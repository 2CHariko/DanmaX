#include "platform/windows/SmtcReader.h"
#include <windows.h>
#include <QElapsedTimer>
#include <QTest>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Media::Control;
namespace {
enum class Fault { None, Operation, Properties, Timeline, Playback, TimelineRead, PlaybackRead, ArtistRead };
struct State { Fault fault{}; int timelineReads{}; int propertyReads{}; bool playing{}; };
struct Properties : implements<Properties, IGlobalSystemMediaTransportControlsSessionMediaProperties> {
    State* state;
    explicit Properties(State* s) : state(s) {}
    hstring Title() { return L"Test title"; }
    hstring Artist() { if (state->fault == Fault::ArtistRead) throw hresult_error(E_FAIL); return L"Test artist"; }
    hstring Subtitle() { return {}; }
    hstring AlbumArtist() { return {}; }
    hstring AlbumTitle() { return {}; }
    int32_t TrackNumber() { return 0; }
    Collections::IVectorView<hstring> Genres() { return nullptr; }
    int32_t AlbumTrackCount() { return 0; }
    IReference<Windows::Media::MediaPlaybackType> PlaybackType() { return nullptr; }
    Windows::Storage::Streams::IRandomAccessStreamReference Thumbnail() { return nullptr; }
};
struct Timeline : implements<Timeline, IGlobalSystemMediaTransportControlsSessionTimelineProperties> {
    State* state;
    explicit Timeline(State* s) : state(s) {}
    TimeSpan StartTime() { ++state->timelineReads; if (state->fault == Fault::TimelineRead) throw hresult_error(RPC_E_DISCONNECTED); return std::chrono::seconds(5); }
    TimeSpan EndTime() { return std::chrono::seconds(125); }
    TimeSpan MinSeekTime() { return StartTime(); }
    TimeSpan MaxSeekTime() { return EndTime(); }
    TimeSpan Position() { return std::chrono::seconds(17); }
    DateTime LastUpdatedTime() { return clock::now(); }
};
struct Playback : implements<Playback, IGlobalSystemMediaTransportControlsSessionPlaybackInfo> {
    State* state;
    explicit Playback(State* s) : state(s) {}
    GlobalSystemMediaTransportControlsSessionPlaybackControls Controls() { return nullptr; }
    GlobalSystemMediaTransportControlsSessionPlaybackStatus PlaybackStatus() { return state->playing ? GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing : GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused; }
    IReference<Windows::Media::MediaPlaybackType> PlaybackType() { return nullptr; }
    IReference<Windows::Media::MediaPlaybackAutoRepeatMode> AutoRepeatMode() { return nullptr; }
    IReference<double> PlaybackRate() { if (state->fault == Fault::PlaybackRead) throw hresult_error(RO_E_CLOSED); return nullptr; }
    IReference<bool> IsShuffleActive() { return nullptr; }
};
struct Session : implements<Session, IGlobalSystemMediaTransportControlsSession> {
    State* state;
    hstring id;
    Session(State* s, hstring name) : state(s), id(name) {}
    hstring SourceAppUserModelId() { return id; }
    IAsyncOperation<GlobalSystemMediaTransportControlsSessionMediaProperties> TryGetMediaPropertiesAsync() {
        ++state->propertyReads;
        if (state->fault == Fault::Operation) return nullptr;
        return properties();
    }
    IAsyncOperation<GlobalSystemMediaTransportControlsSessionMediaProperties> properties() {
        if (state->fault == Fault::Properties) co_return nullptr;
        co_return make<Properties>(state).as<GlobalSystemMediaTransportControlsSessionMediaProperties>();
    }
    GlobalSystemMediaTransportControlsSessionTimelineProperties GetTimelineProperties() {
        if (state->fault == Fault::Timeline) return nullptr;
        return make<Timeline>(state).as<GlobalSystemMediaTransportControlsSessionTimelineProperties>();
    }
    GlobalSystemMediaTransportControlsSessionPlaybackInfo GetPlaybackInfo() {
        if (state->fault == Fault::Playback) return nullptr;
        return make<Playback>(state).as<GlobalSystemMediaTransportControlsSessionPlaybackInfo>();
    }
    // The reader is read-only: any accidental transport command fails the test.
    IAsyncOperation<bool> command() { throw hresult_error(E_ACCESSDENIED); }
    IAsyncOperation<bool> TryPlayAsync() { return command(); }
    IAsyncOperation<bool> TryPauseAsync() { return command(); }
    IAsyncOperation<bool> TryStopAsync() { return command(); }
    IAsyncOperation<bool> TryRecordAsync() { return command(); }
    IAsyncOperation<bool> TryFastForwardAsync() { return command(); }
    IAsyncOperation<bool> TryRewindAsync() { return command(); }
    IAsyncOperation<bool> TrySkipNextAsync() { return command(); }
    IAsyncOperation<bool> TrySkipPreviousAsync() { return command(); }
    IAsyncOperation<bool> TryChangeChannelUpAsync() { return command(); }
    IAsyncOperation<bool> TryChangeChannelDownAsync() { return command(); }
    IAsyncOperation<bool> TryTogglePlayPauseAsync() { return command(); }
    IAsyncOperation<bool> TryChangeAutoRepeatModeAsync(Windows::Media::MediaPlaybackAutoRepeatMode) { return command(); }
    IAsyncOperation<bool> TryChangePlaybackRateAsync(double) { return command(); }
    IAsyncOperation<bool> TryChangeShuffleActiveAsync(bool) { return command(); }
    IAsyncOperation<bool> TryChangePlaybackPositionAsync(int64_t) { return command(); }
    template<class T> event_token TimelinePropertiesChanged(T const&) { return {}; }
    void TimelinePropertiesChanged(event_token const&) {}
    template<class T> event_token PlaybackInfoChanged(T const&) { return {}; }
    void PlaybackInfoChanged(event_token const&) {}
    template<class T> event_token MediaPropertiesChanged(T const&) { return {}; }
    void MediaPropertiesChanged(event_token const&) {}
};
struct Manager : implements<Manager, IGlobalSystemMediaTransportControlsSessionManager> {
    Collections::IVectorView<GlobalSystemMediaTransportControlsSession> sessions{nullptr};
    bool fail{};
    GlobalSystemMediaTransportControlsSession GetCurrentSession() { throw hresult_error(E_ACCESSDENIED); }
    Collections::IVectorView<GlobalSystemMediaTransportControlsSession> GetSessions() { if (fail) throw hresult_error(RPC_E_DISCONNECTED); return sessions; }
    template<class T> event_token CurrentSessionChanged(T const&) { return {}; }
    void CurrentSessionChanged(event_token const&) {}
    template<class T> event_token SessionsChanged(T const&) { return {}; }
    void SessionsChanged(event_token const&) {}
};
IAsyncOperation<SmtcReader::Manager> completed(SmtcReader::Manager manager) { co_return manager; }
IAsyncOperation<SmtcReader::Manager> pending() { co_await resume_after(std::chrono::seconds(3)); co_return nullptr; }
struct Fixture {
    State state;
    com_ptr<Manager> manager = make_self<Manager>();
    GlobalSystemMediaTransportControlsSession session = make<Session>(&state, L"Test.Player").as<GlobalSystemMediaTransportControlsSession>();
    Fixture() { manager->sessions = single_threaded_vector<GlobalSystemMediaTransportControlsSession>({session}).GetView(); }
    auto request() { return [this] { return completed(manager.as<SmtcReader::Manager>()); }; }
};
void disconnected(const SmtcPoll& poll) {
    QVERIFY(!poll.sample.found);
    QVERIFY(!poll.sample.playing);
    QVERIFY(poll.sample.title.isEmpty());
    QVERIFY(poll.sample.identity.isEmpty());
    QCOMPARE(poll.sample.position, 0.0);
    QCOMPARE(poll.sample.duration, 0.0);
    QCOMPARE(poll.sample.playback, MediaSample::Playback::Unknown);
}
} // namespace

class SmtcReaderTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { init_apartment(apartment_type::multi_threaded); }
    void cleanupTestCase() { uninit_apartment(); }
    void unavailableObjectsAndRecovery_data() {
        QTest::addColumn<int>("fault");
        for (int i = static_cast<int>(Fault::Operation); i <= static_cast<int>(Fault::ArtistRead); ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void unavailableObjectsAndRecovery() {
        QFETCH(int, fault);
        Fixture fixture;
        fixture.state.fault = static_cast<Fault>(fault);
        SmtcReader reader(fixture.request());
        const auto failed = reader.poll("Test.Player", 7);
        disconnected(failed);
        QVERIFY(!failed.error.isEmpty());
        QCOMPARE(failed.sample.id, QString("Test.Player"));
        QCOMPARE(failed.sample.generation, 7ULL);
        QVERIFY(failed.sample.observedAt > 0);
        QCOMPARE(failed.sessions.size(), 1);
        if (fixture.state.fault == Fault::Timeline || fixture.state.fault == Fault::Playback)
            QCOMPARE(fixture.state.timelineReads, 0);
        fixture.state.fault = Fault::None;
        const auto recovered = reader.poll("Test.Player", 7);
        QVERIFY2(recovered.error.isEmpty(), qPrintable(recovered.error));
        QVERIFY(recovered.sample.found);
        QCOMPARE(recovered.sample.position, 12.0);
        QCOMPARE(recovered.sample.duration, 120.0);
        QCOMPARE(recovered.sample.rate, 1.0); // Optional PlaybackRate may be null.
        QCOMPARE(recovered.sample.playback, MediaSample::Playback::Paused);
        QCOMPARE(recovered.sample.title, QString("Test title"));
    }
    void nullManagerOperationAndCollection_data() {
        QTest::addColumn<int>("fault");
        QTest::newRow("null-request") << 0;
        QTest::newRow("null-manager") << 1;
        QTest::newRow("null-collection") << 2;
        QTest::newRow("disconnected-manager") << 3;
        QTest::newRow("request-failure") << 4;
    }
    void nullManagerOperationAndCollection() {
        QFETCH(int, fault);
        Fixture fixture;
        auto saved = fixture.manager->sessions;
        int calls = 0;
        SmtcReader reader([&]() -> IAsyncOperation<SmtcReader::Manager> {
            ++calls;
            if (calls == 1 && fault == 0) return nullptr;
            if (calls == 1 && fault == 1) return completed(nullptr);
            if (calls == 1 && fault == 4) throw hresult_error(E_ACCESSDENIED);
            return completed(fixture.manager.as<SmtcReader::Manager>());
        });
        if (fault == 2) fixture.manager->sessions = nullptr;
        if (fault == 3) fixture.manager->fail = true;
        auto result = reader.poll("Test.Player", 1);
        disconnected(result);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.publishSessions);
        QCOMPARE(result.sessions.size(), 0);
        fixture.manager->sessions = saved;
        fixture.manager->fail = false;
        result = reader.poll("Test.Player", 1);
        QCOMPARE(calls, 1); // Back off rather than repeatedly creating managers.
        disconnected(result);
        QTRY_VERIFY_WITH_TIMEOUT(result.sample.found || (result = reader.poll("Test.Player", 2)).sample.found, 1800);
        QCOMPARE(calls, 2);
        QCOMPARE(result.sample.generation, 2ULL);
        QVERIFY(result.publishSessions);
    }
    void brokenSessionDoesNotBlockSelectedSession() {
        Fixture fixture;
        State bad;
        bad.fault = Fault::Properties;
        auto broken = make<Session>(&bad, L"Broken.Player").as<GlobalSystemMediaTransportControlsSession>();
        fixture.manager->sessions = single_threaded_vector<GlobalSystemMediaTransportControlsSession>({nullptr, broken, fixture.session}).GetView();
        SmtcReader reader(fixture.request());
        const auto result = reader.poll("Test.Player", 3);
        QVERIFY(result.sample.found);
        QCOMPARE(result.sample.id, QString("Test.Player"));
        QCOMPARE(result.sessions.size(), 2);
        QVERIFY(!result.error.isEmpty());
    }
    void missingSessionAndSelectionGeneration() {
        Fixture fixture;
        SmtcReader reader(fixture.request());
        QVERIFY(reader.poll("Test.Player", 1).sample.found);
        auto missing = reader.poll("Other.Player", 2);
        disconnected(missing);
        QCOMPARE(missing.sample.id, QString("Other.Player"));
        QCOMPARE(missing.sample.generation, 2ULL);
        fixture.manager->sessions = single_threaded_vector<GlobalSystemMediaTransportControlsSession>().GetView();
        disconnected(reader.poll("Test.Player", 3));
        fixture.manager->sessions = single_threaded_vector<GlobalSystemMediaTransportControlsSession>({fixture.session}).GetView();
        fixture.state.playing = true;
        const auto recovered = reader.poll("Test.Player", 4);
        QVERIFY(recovered.sample.playing);
        QCOMPARE(recovered.sample.generation, 4ULL);
    }
    void cancellationDuringRequest() {
        std::stop_source stop;
        SmtcReader reader([] { return pending(); });
        std::jthread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(60)); stop.request_stop(); });
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = reader.poll("Test.Player", 1, stop.get_token());
        QVERIFY(result.cancelled);
        QVERIFY(elapsed.elapsed() < 600);
        disconnected(result);
    }
    void pendingRequestTimesOut() {
        SmtcReader reader([] { return pending(); });
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = reader.poll("Test.Player", 1);
        disconnected(result);
        QVERIFY(result.error.contains("timed out"));
        QVERIFY(elapsed.elapsed() < 2500);
    }
};
QTEST_GUILESS_MAIN(SmtcReaderTests)
#include "SmtcReaderTests.moc"
