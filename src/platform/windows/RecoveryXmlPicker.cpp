#include "platform/windows/RecoveryXmlPicker.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <qt_windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <atomic>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
constexpr auto cancelled = HRESULT_FROM_WIN32(ERROR_CANCELLED);
constexpr UINT_PTR cancelTimer = 1;
constexpr wchar_t mailboxClass[] = L"DanmaX.RecoveryXmlPicker";

struct NativeDialog {
    HWND owner;
    IFileOpenDialog* picker;
    const std::atomic_bool& invalid;

    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        } else if (message == WM_TIMER && wp == cancelTimer) {
            const auto* self = reinterpret_cast<const NativeDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (self && self->invalid.load()) {
                // The Shell modal loop dispatches this timer on the COM thread.
                // Keep waiting during startup until this specific dialog has an HWND.
                ComPtr<IOleWindow> ole;
                HWND dialog{};
                if (SUCCEEDED(self->picker->QueryInterface(IID_PPV_ARGS(&ole))) &&
                    SUCCEEDED(ole->GetWindow(&dialog)) && IsWindowVisible(dialog) &&
                    GetWindow(dialog, GW_OWNER) == self->owner &&
                    GetWindowThreadProcessId(dialog, nullptr) == GetCurrentThreadId()) {
                    self->picker->Close(cancelled);
                    // Close can return success without ending Show when called
                    // outside a Shell callback. Address only the verified window.
                    if (IsWindowVisible(dialog) && GetWindow(dialog, GW_OWNER) == self->owner &&
                        GetWindowThreadProcessId(dialog, nullptr) == GetCurrentThreadId() &&
                        PostMessageW(dialog, WM_CLOSE, 0, 0))
                        KillTimer(window, cancelTimer);
                }
            }
            return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }
};

HRESULT showPicker(HWND owner, const std::atomic_bool& invalid, std::wstring& path) {
    if (invalid.load()) return cancelled;
    ComPtr<IFileOpenDialog> picker;
    auto hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker));
    if (FAILED(hr)) return hr;
    DWORD options{};
    const COMDLG_FILTERSPEC filter{L"弹幕 XML", L"*.xml"};
    if (FAILED(hr = picker->GetOptions(&options)) ||
        FAILED(hr = picker->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST)) ||
        FAILED(hr = picker->SetFileTypes(1, &filter)) ||
        FAILED(hr = picker->SetTitle(L"为当前视频选择弹幕 XML"))) return hr;

    const auto module = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.hInstance = module;
    cls.lpfnWndProc = NativeDialog::dispatch;
    cls.lpszClassName = mailboxClass;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    NativeDialog state{owner, picker.Get(), invalid};
    const auto mailbox = CreateWindowExW(0, mailboxClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                         nullptr, module, &state);
    if (!mailbox) return HRESULT_FROM_WIN32(GetLastError());
    if (!SetTimer(mailbox, cancelTimer, 20, nullptr)) hr = E_FAIL;
    else if (invalid.load() || !IsWindow(owner) || !IsWindowVisible(owner)) hr = cancelled;
    else hr = picker->Show(owner);
    DestroyWindow(mailbox);
    if (invalid.load()) return cancelled;
    if (hr != S_OK) return hr;

    ComPtr<IShellItem> item;
    PWSTR selected{};
    if (SUCCEEDED(hr = picker->GetResult(&item)) &&
        SUCCEEDED(hr = item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) {
        path = selected;
        CoTaskMemFree(selected);
    }
    return hr;
}
}

struct RecoveryXmlPicker::Request {
    std::atomic_bool invalid{false};
    HRESULT result{E_FAIL};
    std::wstring path;
};

RecoveryXmlPicker::RecoveryXmlPicker(QObject* parent) : QObject(parent) {
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &RecoveryXmlPicker::cancel);
}

RecoveryXmlPicker::~RecoveryXmlPicker() {
    if (request_) request_->invalid.store(true);
}

void RecoveryXmlPicker::open(QWindow* owner) {
    if (request_ || !owner || !owner->isVisible()) return;
    const auto hwnd = reinterpret_cast<HWND>(owner->winId());
    auto request = request_ = std::make_shared<Request>();
    ownerDestroyed_ = connect(owner, &QObject::destroyed, this, &RecoveryXmlPicker::cancel);
    auto* thread = QThread::create([request, hwnd] {
        const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        request->result = hr;
        if (SUCCEEDED(hr)) {
            request->result = showPicker(hwnd, request->invalid, request->path);
            CoUninitialize();
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, request] {
        const bool valid = !request->invalid.load();
        disconnect(ownerDestroyed_);
        request_.reset();
        // Deliver before notifying QML that another request may start.
        if (valid && request->result == S_OK && !request->path.empty())
            emit accepted(QString::fromStdWString(request->path));
        else if (valid && request->result != cancelled)
            emit failed(tr("无法打开或读取 Windows 文件选择器（0x%1）。")
                            .arg(static_cast<quint32>(request->result), 8, 16, QLatin1Char('0')));
        emit busyChanged();
    });
    emit busyChanged();
    thread->start();
}

void RecoveryXmlPicker::cancel() {
    if (!request_ || request_->invalid.exchange(true)) return;
    QTimer::singleShot(2000, this, [this, request = std::weak_ptr<Request>(request_)] {
        if (const auto pending = request.lock(); pending && pending == request_) {
            const auto message = tr("Windows 文件选择器尚未关闭，请手动关闭。");
            qWarning().noquote() << message;
            emit failed(message);
        }
    });
}
