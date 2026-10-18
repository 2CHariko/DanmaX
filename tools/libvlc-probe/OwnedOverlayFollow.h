#pragma once
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <vector>
#include <windows.h>
#include <commctrl.h>

// Isolated GUI-thread experiment. Observe the actual window procedure chain,
// including messages inside the native move loop. Never replace Qt/QWK's WndProc.
class OwnedOverlayFollow final {
    struct Sample {
        qint64 us;
        UINT message;
        int phase, source, before, after;
        bool pureMove;
        qint64 syncUs;
    };
    HWND host_{}, video_{}, overlay_{};
    QPointer<QWindow> videoQt_, overlayQt_;
    bool native_{}, syncing_{};
    int phase_{}, fixes_{}, failures_{}, enter_{}, exit_{}, moving_{}, sizing_{}, clicks_{};
    QElapsedTimer clock_;
    std::vector<Sample> samples_;
    static constexpr UINT_PTR subclassId = 0x444d4657;
    int difference() const {
        RECT a{}, b{};
        if (!GetWindowRect(video_, &a) || !GetWindowRect(overlay_, &b)) return -1;
        return int(std::max({std::abs(a.left - b.left), std::abs(a.top - b.top),
                            std::abs(a.right - b.right), std::abs(a.bottom - b.bottom)}));
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                      UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<OwnedOverlayFollow*>(data);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, procedure, subclassId);
            if (window == self->host_) self->host_ = nullptr;
            if (window == self->video_) self->video_ = nullptr;
            return DefSubclassProc(window, message, w, l);
        }
        if (message == WM_ENTERSIZEMOVE) ++self->enter_;
        if (message == WM_EXITSIZEMOVE) ++self->exit_;
        if (message == WM_MOVING) ++self->moving_;
        if (message == WM_SIZING) ++self->sizing_;
        if (window == self->video_ && message == WM_LBUTTONDOWN) ++self->clicks_;
        const bool observe = message == WM_WINDOWPOSCHANGED || message == WM_EXITSIZEMOVE;
        const int before = observe ? self->difference() : -1;
        bool pureMove = false;
        if (message == WM_WINDOWPOSCHANGED) {
            const auto* p = reinterpret_cast<WINDOWPOS*>(l);
            pureMove = (p->flags & SWP_NOSIZE) && !(p->flags & SWP_NOMOVE);
        }
        // Let Qt and QWindowKit process the message, then follow the video HWND
        // that actually exists now, not a predicted QML layout rectangle.
        const auto result = DefSubclassProc(window, message, w, l);
        if (observe && self->host_ && self->video_ && !self->syncing_) {
            const auto start = self->clock_.nsecsElapsed();
            if (self->native_) self->synchronize();
            if (self->samples_.size() < 20000) self->samples_.push_back({start / 1000, message, self->phase_,
                window == self->host_ ? 0 : 1, before, self->difference(), pureMove,
                (self->clock_.nsecsElapsed() - start) / 1000});
        }
        return result;
    }
public:
    ~OwnedOverlayFollow() { detach(); }
    void detach() {
        if (host_ && IsWindow(host_)) RemoveWindowSubclass(host_, procedure, subclassId);
        if (video_ && IsWindow(video_)) RemoveWindowSubclass(video_, procedure, subclassId);
        host_ = video_ = overlay_ = nullptr;
    }
    bool attach(HWND host, HWND video, HWND overlay, bool native, QWindow* videoQt, QWindow* overlayQt) {
        detach(); host_ = host; video_ = video; overlay_ = overlay; native_ = native;
        videoQt_ = videoQt; overlayQt_ = overlayQt;
        clock_.start(); samples_.reserve(20000);
        return SetWindowSubclass(host, procedure, subclassId, reinterpret_cast<DWORD_PTR>(this))
            && SetWindowSubclass(video, procedure, subclassId, reinterpret_cast<DWORD_PTR>(this));
    }
    void phase(int phase) { phase_ = phase; }
    bool synchronize() {
        if (syncing_ || !host_ || !video_ || !overlay_ || IsIconic(host_)
            || !IsWindowVisible(host_) || !IsWindowVisible(video_)) return false;
        RECT area{};
        if (!GetWindowRect(video_, &area)) { ++failures_; return false; }
        RECT current{};
        if (!GetWindowRect(overlay_, &current)) { ++failures_; return false; }
        if (EqualRect(&area, &current)) return true;
        syncing_ = true;
        // Preserve Qt's window/scene-graph geometry bookkeeping. QWindow maps
        // through its platform window, unlike the potentially fractional QML
        // viewport rectangle. Verify against native pixels before returning.
        if (videoQt_ && overlayQt_) {
            overlayQt_->setGeometry(QRect(videoQt_->mapToGlobal(QPoint()), videoQt_->size()));
            GetWindowRect(overlay_, &current);
            if (EqualRect(&area, &current)) { syncing_ = false; ++fixes_; return true; }
        }
        // Bounded native correction if the public Qt write did not align the
        // actual HWND. No global-DPR arithmetic or extra event loop is used.
        UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER;
        if (area.right - area.left == current.right - current.left &&
            area.bottom - area.top == current.bottom - current.top) flags |= SWP_NOSIZE;
        const bool okay = SetWindowPos(overlay_, nullptr, area.left, area.top,
            area.right - area.left, area.bottom - area.top, flags);
        syncing_ = false;
        if (okay) ++fixes_; else ++failures_;
        return okay;
    }
    int currentDifference() const { return difference(); }
    int count(int phase) const {
        return int(std::count_if(samples_.begin(), samples_.end(), [phase](const auto& s) {
            return s.phase == phase && s.source == 0 && s.pureMove;
        }));
    }
    QJsonObject report() const {
        QJsonArray rows, phases;
        for (const auto& s : samples_) rows.append(QJsonObject{{"us", double(s.us)}, {"message", int(s.message)},
            {"phase", s.phase}, {"source", s.source}, {"beforePx", s.before}, {"afterPx", s.after},
            {"pureMove", s.pureMove}, {"syncUs", double(s.syncUs)}});
        for (int phase = 0; phase <= 3; ++phase) {
            int count = 0, beforeMax = 0, afterMax = 0, beforeBad = 0, afterBad = 0;
            std::vector<qint64> times;
            for (const auto& s : samples_) if (s.phase == phase && s.source == 0 && s.pureMove) {
                ++count; beforeMax = std::max(beforeMax, s.before); afterMax = std::max(afterMax, s.after);
                if (s.before > 0) ++beforeBad;
                if (s.after > 0) ++afterBad;
                times.push_back(s.syncUs);
            }
            std::sort(times.begin(), times.end());
            phases.append(QJsonObject{{"phase", phase}, {"rootPureMoves", count}, {"beforeMaxPx", beforeMax},
                {"afterMaxPx", afterMax}, {"beforeMismatchCount", beforeBad}, {"afterMismatchCount", afterBad},
                {"callbackP95Us", times.empty() ? 0.0 : double(times[std::min(times.size()-1, times.size()*95/100)])}});
        }
        RECT a{}, b{}; GetWindowRect(video_, &a); GetWindowRect(overlay_, &b);
        return {{"strategy", native_ ? "native-hwnd" : "production-controller"},
            {"videoRect", QJsonArray{int(a.left),int(a.top),int(a.right),int(a.bottom)}},
            {"overlayRect", QJsonArray{int(b.left),int(b.top),int(b.right),int(b.bottom)}},
            {"phaseNames", QJsonArray{"transitions", "playing-moves", "paused-moves", "manual"}},
            {"phases", phases}, {"enterSizeMove", enter_}, {"exitSizeMove", exit_}, {"moving", moving_},
            {"sizing", sizing_}, {"videoClicks", clicks_}, {"nativeWrites", fixes_}, {"failures", failures_},
            {"currentDifferencePx", difference()}, {"sampleLimit", 20000}, {"samples", rows}};
    }
};
