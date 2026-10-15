
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QTest>
#include <QDir>
#include <QSignalSpy>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWheelEvent>
#include <QStyleHints>
#include <QInputMethodEvent>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QBuffer>
#include <QScopeGuard>
#include <cmath>
#include <memory>
#include <vector>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "application/AppController.h"
#include "renderer/DanmakuItem.h"
#include "infrastructure/SettingsStore.h"
#include "infrastructure/LogModel.h"
#include "platform/windows/WindowBackdropController.h"
#include <QWKQuick/qwkquickglobal.h>

class UiWorkflowTests : public QObject {
    Q_OBJECT
private:
    void opacityDrag(bool realSession) {
        QTemporaryDir directory;
        AppController backend(directory.path(), nullptr, [](const QString&) { return true; }, directory.path(),
                              [](QWindow*) { return false; });
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        QVERIFY(settings->setValue("micaEnabled", false));
        QVERIFY(settings->setValue("foregroundOnly", false));
        // Deterministic GUI integration: production reader null-return paths are
        // exercised by smtc_reader; inject their disconnected sample here.
        auto* monitor = backend.findChild<MediaMonitor*>();
        QVERIFY(monitor);
        if (!realSession) QObject::disconnect(monitor, &MediaMonitor::sampleReceived, &backend, nullptr);
        const auto session = realSession ? qEnvironmentVariable("DANMAKU_TEST_MEDIA_SESSION") : QStringLiteral("Regression.Player");
        MediaSample latest;
        QJsonArray observations, moves;
        QSignalSpy mediaErrors(monitor, &MediaMonitor::errorOccurred);
        connect(monitor, &MediaMonitor::sampleReceived, &backend, [&](const MediaSample& value) {
            if (!realSession || value.id != session || value.generation != monitor->generation()) return;
            latest = value;
            observations.append(QJsonObject{{"found", value.found}, {"position", value.position},
                {"duration", value.duration}, {"observedAt", value.observedAt},
                {"playback", static_cast<int>(value.playback)}, {"title", value.title}});
        });
        const auto saveReport = qScopeGuard([&] {
            const auto path = qEnvironmentVariable("DANMAKU_TEST_OPACITY_REPORT");
            if (!realSession || path.isEmpty()) return;
            QSaveFile report(path);
            if (report.open(QIODevice::WriteOnly)) {
                report.write(QJsonDocument(QJsonObject{{"session", session}, {"readOnly", true},
                    {"samples", observations}, {"moves", moves}, {"mediaErrors", mediaErrors.count()}}).toJson());
                report.commit();
            }
        });
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* panel = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        auto* toolbar = panel->findChild<QQuickWindow*>("danmakuToolbarWindow");
        auto* bar = toolbar ? toolbar->findChild<QQuickItem*>("danmakuToolbar") : nullptr;
        auto* popup = toolbar ? toolbar->findChild<QQuickItem*>("toolbarOpacityPopup") : nullptr;
        auto* slider = toolbar ? toolbar->findChild<QQuickItem*>("toolbarOpacitySlider") : nullptr;
        auto* overlay = panel->findChild<QQuickWindow*>("danmakuOverlayWindow");
        QVERIFY(panel && toolbar && bar && popup && slider);
        // Use the public renderer property on the overlay, independent of object name.
        if (!overlay) for (auto* window : panel->findChildren<QQuickWindow*>())
            if (window->property("renderer").isValid()) { overlay = window; break; }
        QVERIFY(overlay);
        auto* renderer = qobject_cast<DanmakuItem*>(overlay->property("renderer").value<QObject*>());
        QVERIFY(renderer);
        backend.beginDemo(50);
        backend.stop();
        backend.selectSession(session);
        backend.start(false);
        auto sample = [&](bool found) {
            MediaSample value;
            value.id = QStringLiteral("Regression.Player");
            value.generation = monitor->generation();
            value.observedAt = MediaMonitor::monotonicSeconds();
            value.found = found;
            if (found) {
                value.title = QStringLiteral("Paused test player");
                value.identity = value.title;
                value.position = 12;
                value.duration = 120;
                value.playback = MediaSample::Playback::Paused;
            }
            QVERIFY(QMetaObject::invokeMethod(&backend, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, value)));
        };
        if (realSession) {
            QTRY_VERIFY_WITH_TIMEOUT(observations.size() >= 3 && latest.found && !backend.mediaTitle().isEmpty(), 10000);
            QVERIFY(!backend.manualMode());
            QVERIFY(latest.duration > 0);
        } else {
            sample(true);
            QCOMPARE(backend.playbackState(), AppController::Paused);
        }
        bar->setProperty("pinned", true);
        QTRY_VERIFY(toolbar->isVisible());
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        popup->setVisible(true);
        QTest::qWait(80);
        auto point = [&](double fraction) {
            const auto left = slider->property("leftPadding").toDouble();
            const auto right = slider->property("rightPadding").toDouble();
            return slider->mapToScene(QPointF(left + (slider->width() - left - right) * fraction, slider->height() / 2)).toPoint();
        };
        QTest::mousePress(toolbar, Qt::LeftButton, Qt::NoModifier, point(0.5));
        QTRY_VERIFY(slider->property("pressed").toBool());
        const int total = backend.total();
        if (!realSession) QVERIFY(QMetaObject::invokeMethod(monitor, "errorOccurred", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("SMTC returned null timeline properties"))));
        QVERIFY(backend.error().isEmpty());
        QElapsedTimer elapsed;
        elapsed.start();
        const int samplesBeforeDrag = observations.size();
        for (int i = 0; i < (realSession ? 120 : 12); ++i) {
            if (!realSession) {
                sample(i % 2 != 0);
                QCOMPARE(backend.playbackState(), i % 2 ? AppController::Paused : AppController::WaitingForPlayer);
            }
            QTest::mouseMove(toolbar, point(i % 2 ? 0.8 : 0.2), 15);
            QTest::qWait(realSession ? 250 : 30);
            const double expected = settings->values()["opacity"].toDouble();
            QVERIFY(expected >= 0.05 && expected <= 1.0);
            QVERIFY(i % 2 ? expected > 0.6 : expected < 0.4);
            QTRY_COMPARE(renderer->opacity(), expected);
            if (realSession) {
                QVERIFY(MediaMonitor::monotonicSeconds() - latest.observedAt < 2.5);
                if (latest.found) QVERIFY(std::abs(backend.position() - latest.position) < 0.5);
                moves.append(QJsonObject{{"elapsedMs", elapsed.elapsed()}, {"opacity", expected},
                    {"renderOpacity", renderer->opacity()}, {"position", backend.position()},
                    {"state", static_cast<int>(backend.playbackState())}});
            } else QCOMPARE(backend.position(), 12.0);
            QCOMPARE(backend.total(), total);
            QVERIFY(backend.running());
            QVERIFY(backend.error().isEmpty());
        }
        QTest::mouseRelease(toolbar, Qt::LeftButton, Qt::NoModifier, point(0.8));
        QVERIFY(settings->values()["opacity"].toDouble() > 0.6);
        QTRY_VERIFY(!slider->property("pressed").toBool());
        QVERIFY(settings->retrySave());
        SettingsStore restored(directory.path());
        QCOMPARE(restored.values()["opacity"], settings->values()["opacity"]);
        if (realSession) {
            QVERIFY(observations.size() >= samplesBeforeDrag + 20);
            QVERIFY(latest.found);
            QVERIFY(backend.playbackState() != AppController::WaitingForPlayer);
        }
        backend.stop();
        QCOMPARE(warnings.count(), 0);
    }
private slots:
    void coverPlaceholderWorkflow() {
        QTemporaryDir directory;
        const auto coverPath = directory.filePath("cover.png");
        QImage cover(140, 210, QImage::Format_RGB32); cover.fill(Qt::darkCyan); QVERIFY(cover.save(coverPath));
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.loadData(
            "import QtQuick\nimport QtQuick.Window\nimport \"components\"\nimport \"theme\"\n"
            "Window { id: harness; width: 320; height: 300; visible: true\n"
            "property bool ownerVisible: true\n"
            "readonly property int animatedCovers: Ui.activeCoverPlaceholders\n"
            "function motion(enabled: bool) { Ui.systemAnimationsEnabled = enabled }\n"
            "QtObject { id: service; objectName: \"coverService\"\n"
            "property url source: \"\"\nproperty url url: \"\"\nproperty bool loading: false\n"
            "function createRequest(owner) { return service }\nfunction retry() {}\n}\n"
            "AnimeCard { anchors.centerIn: parent; visible: harness.ownerVisible; coverCache: service\n"
            "animeData: ({title: \"Missing cover\", cover: \"https://example.test/cover\", score: \"7.1\", airDate: \"2026-10-15\"})\n"
            "}\n}", QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/CoverHarness.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* service = window->findChild<QObject*>("coverService"); QVERIFY(service);
        auto* image = window->findChild<QQuickItem*>("cachedCoverImage"); QVERIFY(image);
        auto* placeholder = image->findChild<QQuickItem*>("coverPlaceholder"); QVERIFY(placeholder);
        QVERIFY(placeholder->isVisible()); QVERIFY(!placeholder->property("pulsing").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "motion", Q_ARG(bool, true)));
        service->setProperty("loading", true);
        QTRY_VERIFY(placeholder->property("pulsing").toBool()); QCOMPARE(window->property("animatedCovers").toInt(), 1);
        window->setProperty("ownerVisible", false); QCOMPARE(window->property("animatedCovers").toInt(), 0);
        window->setProperty("ownerVisible", true); QTRY_VERIFY(placeholder->property("pulsing").toBool());
        QVERIFY(QMetaObject::invokeMethod(window, "motion", Q_ARG(bool, false)));
        QVERIFY(!placeholder->property("pulsing").toBool()); QCOMPARE(window->property("animatedCovers").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(window, "motion", Q_ARG(bool, true)));
        service->setProperty("loading", false); QVERIFY(!placeholder->property("pulsing").toBool());
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/cover-placeholder/");
        QVERIFY(QDir().mkpath(evidence));
        QVERIFY(window->grabWindow().save(evidence + "missing-cover.png"));
        service->setProperty("loading", true); QTRY_VERIFY(placeholder->property("pulsing").toBool());
        service->setProperty("source", QUrl::fromLocalFile(coverPath)); service->setProperty("loading", false);
        QTRY_COMPARE(image->property("status").toInt(), 1);
        QTRY_COMPARE(image->property("revealProgress").toDouble(), 1.0);
        QVERIFY(!placeholder->isVisible()); QCOMPARE(window->property("animatedCovers").toInt(), 0);
        window->setProperty("ownerVisible", false); window->setProperty("ownerVisible", true);
        QCOMPARE(image->property("revealProgress").toDouble(), 1.0);
        service->setProperty("source", QUrl());
        service->setProperty("loading", true); service->setProperty("source", QUrl::fromLocalFile(coverPath)); service->setProperty("loading", false);
        QTRY_COMPARE(image->property("status").toInt(), 1);
        QVERIFY(!placeholder->property("revealed").toBool());
        QCOMPARE(window->property("animatedCovers").toInt(), 0);
        QCOMPARE(warnings.count(), 0);
    }
    void loadingPlaceholderWorkflow() {
        bool enabled = true;
        SystemUiPreferences preferences(nullptr, [&] { return enabled; });
        QSignalSpy changed(&preferences, &SystemUiPreferences::changed);
        enabled = false; preferences.refresh(); QVERIFY(!preferences.animationsEnabled()); QCOMPARE(changed.count(), 1);
        preferences.refresh(); QCOMPARE(changed.count(), 1);
        enabled = true; preferences.refresh(); QVERIFY(preferences.animationsEnabled()); QCOMPARE(changed.count(), 2);

        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import "components"
import "theme"
Window {
    id: harness
    width: 600; height: 400; visible: true
    property bool busy: false
    property bool available: false
    property bool ownerVisible: true
    function motion(enabled: bool) { Ui.systemAnimationsEnabled = enabled }
    ColumnLayout {
        anchors.fill: parent
        visible: harness.ownerVisible
        LoadingPlaceholder {
            objectName: "placeholder"
            loading: busy; hasContent: available; appearance: "episodes"
            onContentReady: reveal.restart()
        }
        Rectangle { id: content; objectName: "content"; visible: available; Layout.fillWidth: true; implicitHeight: 80; color: Ui.cardBackground }
        ContentReveal { id: reveal; target: content }
        Item { Layout.fillHeight: true }
    }
})", QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/LoadingHarness.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* placeholder = window->findChild<QQuickItem*>("placeholder"); QVERIFY(placeholder);
        auto* content = window->findChild<QQuickItem*>("content"); QVERIFY(content);
        QSignalSpy ready(placeholder, SIGNAL(contentReady()));
        QVERIFY(QMetaObject::invokeMethod(window, "motion", Q_ARG(bool, true)));
        window->setProperty("busy", true);
        QVERIFY(!placeholder->property("showing").toBool());
        QTRY_VERIFY(placeholder->property("showing").toBool());
        QTRY_VERIFY(placeholder->property("pulsing").toBool());
        window->setProperty("available", true); window->setProperty("busy", false);
        QTRY_COMPARE(ready.count(), 1);
        QTRY_COMPARE(content->opacity(), 1.0);
        QVERIFY(!placeholder->isVisible());
        // Existing content remains visible and does not replay a reveal on refresh/re-entry.
        window->setProperty("busy", true); QTest::qWait(200);
        QVERIFY(!placeholder->isVisible()); QCOMPARE(content->opacity(), 1.0);
        window->setProperty("ownerVisible", false); window->setProperty("ownerVisible", true);
        window->setProperty("busy", false); QCOMPARE(ready.count(), 1);
        // Fast completions skip the delayed placeholder and its transition.
        window->setProperty("available", false); window->setProperty("busy", true);
        window->setProperty("available", true); window->setProperty("busy", false);
        QTest::qWait(200); QCOMPARE(ready.count(), 1);
        // Hidden pages stop their placeholder animation, including mid-request navigation.
        window->setProperty("available", false); window->setProperty("busy", true);
        QTRY_VERIFY(placeholder->property("showing").toBool());
        window->setProperty("ownerVisible", false);
        QVERIFY(!placeholder->property("pulsing").toBool());
        window->setProperty("busy", false); QCOMPARE(ready.count(), 1);
        window->setProperty("ownerVisible", true);
        QVERIFY(QMetaObject::invokeMethod(window, "motion", Q_ARG(bool, false)));
        window->setProperty("busy", true);
        QTRY_VERIFY(placeholder->property("showing").toBool());
        QVERIFY(!placeholder->property("pulsing").toBool());
        window->setProperty("available", true); window->setProperty("busy", false);
        QTRY_COMPARE(content->opacity(), 1.0);
        QCOMPARE(ready.count(), 2);
        QVERIFY(QMetaObject::invokeMethod(window, "motion", Q_ARG(bool, true)));
        QCOMPARE(warnings.count(), 0);
    }
    void onlineLoadingWorkflow() {
        QTemporaryDir directory;
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                auto request = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, &server, [&, socket, request] {
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n")) return;
                    disconnect(socket, &QTcpSocket::readyRead, &server, nullptr);
                    const auto target = request->split(' ').value(1);
                    const QByteArray body = target.contains("search/anime")
                        ? R"({"animes":[{"animeId":42,"animeTitle":"加载测试"}]})"
                        : R"({"bangumi":{"episodes":[{"episodeId":42001,"episodeTitle":"第1话"}]}})";
                    QTimer::singleShot(450, socket, [guard = QPointer<QTcpSocket>(socket), body] {
                        if (!guard || guard->state() != QAbstractSocket::ConnectedState) return;
                        guard->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        guard->disconnectFromHost();
                    });
                });
            }
        });
        DanmakuLibrary library(directory.path());
        library.setServers({QString("http://127.0.0.1:%1").arg(server.serverPort())});
        QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.setInitialProperties({{"library", QVariant::fromValue(&library)}});
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "components"
import "theme"
ApplicationWindow {
    id: harness
    required property QtObject library
    width: 600; height: 650; visible: true
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16
        OnlineDanmakuPane { library: harness.library; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
    }
})", QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/OnlineLoadingHarness.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* search = window->findChild<QQuickItem*>("onlineAnimeSkeleton"); QVERIFY(search);
        auto* episodes = window->findChild<QQuickItem*>("onlineEpisodeSkeleton"); QVERIFY(episodes);
        auto* box = window->findChild<QQuickItem*>("episodeChoice"); QVERIFY(box);
        library.searchAnime("测试"); QTRY_VERIFY(search->property("showing").toBool());
        QTRY_VERIFY(!library.busy()); QVERIFY(!search->isVisible());
        library.selectAnime("42"); QTRY_VERIFY(episodes->property("showing").toBool());
        QVERIFY(!box->isVisible());
        library.cancel(); QVERIFY(!episodes->isVisible()); QVERIFY(box->isVisible());
        library.selectAnime("42"); QTRY_VERIFY(episodes->property("showing").toBool());
        QTRY_VERIFY(!library.busy()); QTRY_VERIFY(box->isVisible()); QTRY_COMPARE(box->opacity(), 1.0);
        QCOMPARE(box->property("count").toInt(), 1);
        box->forceActiveFocus(); QTest::keyClick(window, Qt::Key_F4);
        auto* popup = box->property("popup").value<QObject*>(); QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(library.selectedEpisode(), QString("42001"));
        QCOMPARE(warnings.count(), 0);
    }
    void metadataCacheOfflineWorkflow() {
        QTemporaryDir directory;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto base = QString("http://127.0.0.1:%1").arg(server.serverPort());
        const auto cover = base + "/cover";
        QImage image(120, 180, QImage::Format_RGB32); image.fill(QColor("#008080"));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        const QJsonObject subject{{"id", 123}, {"type", 2}, {"name_cn", "离线缓存回归番剧"},
            {"summary", QStringLiteral("重启后从本地缓存显示详情与封面。\n").repeated(35)},
            {"images", QJsonObject{{"large", cover}}}, {"rating", QJsonObject{{"score", 8.2}}}};
        QJsonArray calendar;
        for (int day = 1; day <= 7; ++day) calendar.append(QJsonObject{{"weekday", QJsonObject{{"id", day}}}, {"items", QJsonArray{subject}}});
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection(); auto request = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, &server, [&, socket, request] {
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n")) return;
                    const auto length = QRegularExpression("(?im)^Content-Length:\\s*(\\d+)").match(QString::fromLatin1(*request)).captured(1).toInt();
                    if (request->size() < request->indexOf("\r\n\r\n") + 4 + length) return;
                    disconnect(socket, &QTcpSocket::readyRead, &server, nullptr); ++requests;
                    const auto target = request->split(' ').value(1);
                    QByteArray body;
                    if (target.startsWith("/cover")) body = png;
                    else if (target.startsWith("/calendar")) body = QJsonDocument(calendar).toJson();
                    else if (target.startsWith("/v0/search")) body = QJsonDocument(QJsonObject{{"data", QJsonArray{subject}}, {"total", 1}}).toJson();
                    else if (target.startsWith("/v0/episodes")) body = R"({"data":[{"id":901,"sort":1,"name_cn":"离线分集","type":0}],"total":1})";
                    else body = QJsonDocument(subject).toJson();
                    const auto delay = target.startsWith("/v0/episodes") ? 800
                        : target.startsWith("/calendar") || target.startsWith("/v0/search") ? 500 : 0;
                    QTimer::singleShot(delay, socket, [guard = QPointer<QTcpSocket>(socket), body] {
                        if (!guard || guard->state() != QAbstractSocket::ConnectedState) return;
                        guard->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        guard->disconnectFromHost();
                    });
                });
            }
        });
        std::function<QQuickItem*(QQuickItem*, const QString&)> find = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto* child : parent->childItems()) if (auto* item = find(child, name)) return item;
            return nullptr;
        };
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/metadata-cache/") +
            (qEnvironmentVariable("QT_QUICK_BACKEND") == "software" ? "software" : "native") +
            (qEnvironmentVariableIsSet("DANMAKU_UI_LARGE_TEXT") ? "-scaled" : "");
        QVERIFY(QDir().mkpath(evidence));
        for (int restart = 0; restart < 2; ++restart) {
            const auto before = requests;
            AppController backend(directory.filePath("data"), nullptr, {}, directory.filePath("cache"));
            auto* metadata = qobject_cast<AnimeMetadataService*>(backend.metadata()); metadata->setBaseUrl(base);
            auto* settings = qobject_cast<SettingsStore*>(backend.settings()); QVERIFY(settings->setValue("micaEnabled", false));
            QQmlApplicationEngine engine; QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
            engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
            engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window); window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            if (!restart) {
                auto* skeleton = window->findChild<QQuickItem*>("homeHeroSkeleton"); QVERIFY(skeleton);
                QTRY_VERIFY(skeleton->property("showing").toBool());
                QVERIFY(window->grabWindow().save(evidence + "/home-loading.png"));
            }
            QTRY_VERIFY(!metadata->loading() && !metadata->weeklyCalendar().isEmpty());
            QTRY_VERIFY(metadata->heroAnime().contains("summary"));
            QTRY_VERIFY(metadata->coverCacheBytes() > 0);
            const auto coverReady = [&] {
                for (auto* item : window->findChildren<QQuickItem*>("cachedCoverImage"))
                    if (item->isVisible() && item->property("status").toInt() == 1 && item->property("source").toUrl().isLocalFile()) return true;
                return false;
            };
            QTRY_VERIFY(coverReady());
            auto* home = window->findChild<QQuickItem*>("homePage"); auto* field = window->findChild<QQuickItem*>("homeAnimeSearch");
            QVERIFY(home && field);
            // A retained StackLayout page must keep its pictures and metadata on re-entry.
            QList<QPointer<QQuickItem>> readyCovers;
            QList<QUrl> sources;
            std::vector<std::unique_ptr<QSignalSpy>> sourceChanges;
            for (auto* item : home->findChildren<QQuickItem*>("cachedCoverImage")) {
                if (!item->isVisible()) continue;
                QTRY_COMPARE(item->property("status").toInt(), 1);
                readyCovers.append(item); sources.append(item->property("source").toUrl());
                auto* request = item->property("request").value<QObject*>(); QVERIFY(request);
                sourceChanges.push_back(std::make_unique<QSignalSpy>(request, SIGNAL(sourceChanged())));
            }
            QVERIFY(!readyCovers.isEmpty());
            const auto requestCount = requests;
            QSignalSpy calendarChanges(metadata, &AnimeMetadataService::calendarChanged);
            QSignalSpy loadingChanges(metadata, &AnimeMetadataService::stateChanged);
            for (int visit = 0; visit < 3; ++visit) {
                auto* away = find(window->contentItem(), visit % 2 ? "navigation2" : "navigation1"); QVERIFY(away);
                away->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(!home->isVisible());
                for (qsizetype i = 0; i < readyCovers.size(); ++i) {
                    QVERIFY(readyCovers[i]); QCOMPARE(readyCovers[i]->property("source").toUrl(), sources[i]);
                }
                auto* back = find(window->contentItem(), "navigation0"); QVERIFY(back);
                back->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(home->isVisible());
                QVERIFY(!metadata->loading());
                for (qsizetype i = 0; i < readyCovers.size(); ++i) {
                    QVERIFY(readyCovers[i]); QCOMPARE(readyCovers[i]->property("status").toInt(), 1);
                    QCOMPARE(readyCovers[i]->property("source").toUrl(), sources[i]);
                    QCOMPARE(sourceChanges[static_cast<size_t>(i)]->count(), 0);
                }
            }
            QTest::qWait(60); QCOMPARE(requests, requestCount);
            QCOMPARE(calendarChanges.count(), 0); QCOMPARE(loadingChanges.count(), 0);
            auto* editor = field->property("contentItem").value<QQuickItem*>(); QVERIFY(editor);
            editor->setProperty("text", "cached"); editor->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Return);
            if (!restart) {
                auto* skeleton = window->findChild<QQuickItem*>("animeSearchSkeleton"); QVERIFY(skeleton);
                QTRY_VERIFY(skeleton->property("showing").toBool());
                QVERIFY(window->grabWindow().save(evidence + "/search-loading.png"));
            }
            QTRY_VERIFY(!metadata->searching() && metadata->searchResults().size() == 1);
            auto* result = find(home, "animeSearchResult0"); QVERIFY(result); result->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
            auto* dialog = home->findChild<QObject*>("animeDetailDialog"); QVERIFY(dialog);
            if (!restart) {
                auto* content = dialog->property("contentItem").value<QQuickItem*>(); QVERIFY(content);
                auto* skeleton = find(content, "animeEpisodesSkeleton"); QVERIFY(skeleton);
                auto* count = find(content, "animeEpisodeCount"); QVERIFY(count);
                QTRY_VERIFY(skeleton->property("showing").toBool());
                QVERIFY(metadata->loadingEpisodes()); QVERIFY(!metadata->loadingSubject());
                QVERIFY(count->property("text").toString().contains(QStringLiteral("加载")));
                QVERIFY(window->grabWindow().save(evidence + "/episodes-loading.png"));
            }
            QTRY_VERIFY(dialog->property("opened").toBool() && !metadata->loadingDetail());
            QCOMPARE(metadata->currentDetail().value("episodes").toList().size(), 1);
            QVERIFY(metadata->detailError().isEmpty() && metadata->searchError().isEmpty() && metadata->error().isEmpty());
            if (restart) {
                QVERIFY(window->grabWindow().save(evidence + "/offline-detail.png"));
                auto* content = dialog->property("contentItem").value<QQuickItem*>(); QVERIFY(content);
                auto* flick = content->property("contentItem").value<QQuickItem*>(); QVERIFY(flick);
                const auto point = content->mapToScene(QPointF(content->width()/2, content->height()/2));
                QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(window, &wheel); QTRY_VERIFY(flick->property("contentY").toDouble() > 0);
                QVERIFY(window->grabWindow().save(evidence + "/offline-detail-scrolled.png"));
            }
            QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(!dialog->property("visible").toBool());
            editor->setProperty("text", ""); QTest::qWait(40);
            if (restart) {
                QCOMPARE(requests, before);
                QVERIFY(window->grabWindow().save(evidence + "/offline-home.png"));
                auto* nav = find(window->contentItem(), "navigation3"); QVERIFY(nav); nav->forceActiveFocus(); QTest::keyClick(window, Qt::Key_Space);
                auto* settingsPage = window->findChild<QQuickItem*>("settingsPage"); QVERIFY(settingsPage);
                auto* flick = settingsPage->property("contentItem").value<QQuickItem*>(); QVERIFY(flick);
                auto* clear = find(settingsPage, "clearMetadataCacheCard"); QVERIFY(clear);
                for (const auto& theme : {QString("light"), QString("dark")}) for (int width : {520, 800, 1080}) {
                    QVERIFY(settings->setValue("theme", theme)); window->setMinimumWidth(480); window->resize(width, 640); QTest::qWait(40);
                    flick->setProperty("contentY", 0);
                    const auto point = settingsPage->mapToScene(QPointF(settingsPage->width()/2, settingsPage->height()/2));
                    QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), {}, QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(window, &wheel); QTRY_VERIFY(flick->property("contentY").toDouble() > 0);
                    auto* group = settingsPage->findChild<QQuickItem*>("metadataCacheGroup"); QVERIFY(group);
                    const auto y = group->mapToItem(flick->property("contentItem").value<QQuickItem*>(), QPointF()).y();
                    flick->setProperty("contentY", std::max(0.0, std::min(y - 16, flick->property("contentHeight").toDouble() - flick->height())));
                    clear->forceActiveFocus(); QTest::qWait(40);
                    QVERIFY(clear->hasActiveFocus());
                    const auto right = clear->mapToItem(settingsPage, QPointF(clear->width(), 0)).x(); QVERIFY(right <= settingsPage->width() + 1);
                    QVERIFY(window->grabWindow().save(evidence + QString("/settings-%1-%2.png").arg(theme).arg(width)));
                }
                QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(metadata->cacheCleared());
                QCOMPARE(metadata->jsonCacheBytes(), 0); QCOMPARE(metadata->coverCacheBytes(), 0);
                QTest::qWait(80); QCOMPARE(requests, before);
                QVERIFY(window->grabWindow().save(evidence + "/cleared-settings.png"));
            }
            QCOMPARE(warnings.count(), 0);
            if (!restart) server.close(); // Same URL, no server listening for the new service/engine instance.
        }
    }
    void homeAnimeSearchWorkflow() {
        const bool live = qEnvironmentVariableIsSet("DANMAKU_TEST_METADATA_LIVE");
        QTemporaryDir directory;
        const auto coverPath = directory.filePath("cover.png");
        QImage coverImage(48, 64, QImage::Format_RGB32);
        coverImage.fill(QColor("#4488aa"));
        QVERIFY(coverImage.save(coverPath));
        const auto cover = QUrl::fromLocalFile(coverPath).toString();
        QTcpServer server;
        int requests = 0, failures = 0;
        QJsonArray items;
        for (int i = 0; i < 20; ++i) items.append(QJsonObject{{"id", 101 + i}, {"type", 2},
            {"name_cn", QStringLiteral("测试番剧 %1：很长的中文名称用于检查窄屏换行").arg(i + 1)},
            {"name", QStringLiteral("Original title %1 with a long name for wrapping").arg(i + 1)},
            {"date", "2026-10-15"}, {"rating", QJsonObject{{"score", 8.4}}}, {"images", QJsonObject{{"large", cover}}}});
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, &server, [&, socket, bytes] {
                    bytes->append(socket->readAll());
                    const auto headerEnd = bytes->indexOf("\r\n\r\n") + 4;
                    if (headerEnd < 4) return;
                    const auto length = QRegularExpression("(?im)^Content-Length:\\s*(\\d+)")
                        .match(QString::fromLatin1(bytes->left(headerEnd))).captured(1).toLongLong();
                    if (bytes->size() < headerEnd + length) return;
                    disconnect(socket, &QTcpSocket::readyRead, &server, nullptr);
                    const auto target = bytes->split(' ').value(1);
                    QByteArray body;
                    int status = 200, delay = 0;
                    if (target.startsWith("/v0/search/subjects")) {
                        ++requests;
                        const auto keyword = QJsonDocument::fromJson(bytes->mid(headerEnd)).object().value("keyword").toString();
                        if (keyword == "error" && failures++ == 0) status = 503;
                        if (keyword == "slow") delay = 220;
                        body = QJsonDocument(QJsonObject{{"total", keyword == "empty" ? 0 : 80},
                            {"data", keyword == "empty" ? QJsonArray{} : items}}).toJson();
                    } else if (target.startsWith("/v0/subjects/")) {
                        body = QJsonDocument(QJsonObject{{"id", target.mid(13).toInt()},
                            {"name_cn", QStringLiteral("搜索结果详情")}, {"summary", QStringLiteral("简介与分集复用现有详情流程。")}}).toJson();
                    } else if (target.startsWith("/v0/episodes")) body = R"({"data":[],"total":0})";
                    else body = R"([{"weekday":{"id":1},"items":[]}])";
                    QTimer::singleShot(delay, &server, [guard = QPointer<QTcpSocket>(socket), body, status] {
                        if (!guard || guard->state() != QAbstractSocket::ConnectedState) return;
                        guard->write("HTTP/1.1 " + QByteArray::number(status) + " OK\r\nContent-Length: " +
                            QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        guard->disconnectFromHost();
                    });
                });
            }
        });
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        AppController backend(directory.filePath("data"), nullptr, {}, directory.filePath("cache"));
        auto* metadata = qobject_cast<AnimeMetadataService*>(backend.metadata());
        if (!live) metadata->setBaseUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        QVERIFY(settings->setValue("micaEnabled", false));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QStringList loadErrors;
        for (const auto& signal : warnings) for (const auto& error : qvariant_cast<QList<QQmlError>>(signal.first())) loadErrors << error.toString();
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(loadErrors.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* home = window->findChild<QQuickItem*>("homePage");
        const auto available = window->screen()->availableGeometry().size() - QSize(32, 32);
        QCOMPARE(window->minimumWidth(), std::min(800, std::max(1, available.width())));
        QCOMPARE(window->minimumHeight(), std::min(600, std::max(1, available.height())));
        auto* field = window->findChild<QQuickItem*>("homeAnimeSearch");
        QVERIFY(field);
        auto* searchHint = window->findChild<QQuickItem*>("homeAnimeSearchHint");
        QVERIFY(searchHint && searchHint->isVisible());
        QCOMPARE(searchHint->property("text").toString(), QStringLiteral("搜索番剧"));
        auto* heading = home->findChild<QQuickItem*>("pageHeading");
        QVERIFY(heading && heading->isVisible());
        auto* titleBar = window->findChild<QQuickItem*>("mainTitleBar");
        auto* captions = window->findChild<QQuickItem*>("mainCaptionButtons");
        QVERIFY(titleBar && captions);
        QVERIFY(field->mapToScene(QPointF()).y() >= 7.5);
        QTRY_VERIFY(field->mapToScene(QPointF(0, field->height())).y() <= home->mapToScene(QPointF()).y());
        std::function<QQuickItem*(QQuickItem*, const QString&)> find = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto* child : parent->childItems()) if (auto* result = find(child, name)) return result;
            return nullptr;
        };
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, field->mapToScene(QPointF(24, field->height() / 2)).toPoint());
        auto* editor = field->property("contentItem").value<QQuickItem*>();
        QVERIFY(editor);
        const auto indicatorItem = [&](const char* name) -> QQuickItem* {
            const auto* indicator = field->property(name).value<QObject*>();
            return indicator ? indicator->property("indicator").value<QQuickItem*>() : nullptr;
        };
        auto* searchIcon = indicatorItem("searchIndicator");
        auto* clearIcon = indicatorItem("clearIndicator");
        QVERIFY(searchIcon && clearIcon);
        editor->forceActiveFocus();
        if (live) for (char letter : QByteArray("CLANNAD")) QTest::keyClick(window, letter);
        else {
            QInputMethodEvent preedit(QStringLiteral("ceshi"), {});
            QCoreApplication::sendEvent(editor, &preedit);
            QVERIFY(!searchHint->isVisible());
            QCOMPARE(requests, 0);
            QInputMethodEvent commit;
            commit.setCommitString(QStringLiteral("测试番剧"));
            QCoreApplication::sendEvent(editor, &commit);
            QCOMPARE(field->property("text").toString(), QStringLiteral("测试番剧"));
        }
        QVERIFY(!metadata->searching());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!metadata->searching() && !metadata->searchResults().isEmpty(), 20000);
        QVERIFY2(metadata->searchError().isEmpty(), qPrintable(metadata->searchError()));
        if (live) QTRY_VERIFY_WITH_TIMEOUT(metadata->coverCacheBytes() > 0, 20000);
        QVERIFY(!heading->isVisible());
        auto* first = find(home, "animeSearchResult0");
        QVERIFY(first && first->isVisible());
        if (!live) QCOMPARE(metadata->searchResults().size(), 20);
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_VERIFY(first->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Space);
        auto* dialog = home->findChild<QObject*>("animeDetailDialog");
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool() && !metadata->loadingDetail(), 20000);
        QCOMPARE(metadata->currentDetail().value("id"), metadata->searchResults().first().toMap().value("id"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
#if defined(QT_DEBUG)
        const auto build = QStringLiteral("debug");
#else
        const auto build = QStringLiteral("release");
#endif
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/home-search/") +
            (live ? "live-" : "") + build + (qEnvironmentVariable("QT_QUICK_BACKEND") == "software" ? "-software" : "-native") +
            (qEnvironmentVariableIsSet("DANMAKU_UI_LARGE_TEXT") ? "-scaled" : "");
        QVERIFY(QDir().mkpath(evidence));
        auto* flick = home->property("contentItem").value<QQuickItem*>();
        QVERIFY(flick);
        for (auto theme : {"light", "dark"}) {
            QVERIFY(settings->setValue("theme", theme));
            for (int width : {1080, 1008, 800}) {
                window->resize(width, width == 800 ? 600 : 760);
                flick->setProperty("contentY", 0);
                QTest::qWait(100);
                QVERIFY(field->width() <= field->parentItem()->width());
                QVERIFY(std::abs(field->x() + field->width() / 2 - field->parentItem()->width() / 2) < 1);
                QVERIFY(field->mapToScene(QPointF()).y() >= 0);
                QVERIFY(field->mapToScene(QPointF(0, field->height())).y() <= titleBar->height());
                QVERIFY(captions->mapToScene(QPointF()).x()
                    - field->mapToScene(QPointF(field->width(), 0)).x() >= 47.5);
#if defined(Q_OS_WIN)
                const auto hwnd = reinterpret_cast<HWND>(window->winId());
                const auto hitAt = [&](const QPointF& scene) {
                    POINT pixel{qRound(scene.x() * window->devicePixelRatio()),
                        qRound(scene.y() * window->devicePixelRatio())};
                    ClientToScreen(hwnd, &pixel);
                    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(pixel.x, pixel.y));
                };
                QCOMPARE(hitAt(field->mapToScene(QPointF(field->width() / 2, field->height() / 2))), LRESULT(HTCLIENT));
                QCOMPARE(hitAt(QPointF(captions->mapToScene(QPointF()).x() - 24, titleBar->height() / 2)), LRESULT(HTCAPTION));
                auto* maxButton = captions->property("maxBtn").value<QQuickItem*>();
                QVERIFY(maxButton);
                QCOMPARE(hitAt(maxButton->mapToScene(QPointF(maxButton->width() / 2, maxButton->height() / 2))), LRESULT(HTMAXBUTTON));
#endif
                const auto right = first->mapToItem(home, QPointF(first->width(), 0)).x();
                QVERIFY(right <= home->width());
                QVERIFY(window->grabWindow().save(evidence + QString("/%1-%2.png").arg(theme).arg(width)));
            }
        }
        flick->setProperty("contentY", 0);
        const auto searchY = field->mapToScene(QPointF()).y();
        const auto point = home->mapToScene(QPointF(home->width() / 2, home->height() / 2));
        QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), {}, QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        if (!live || flick->property("contentHeight").toDouble() > flick->height() + 1)
            QTRY_VERIFY(flick->property("contentY").toDouble() > 0);
        else QCOMPARE(flick->property("contentY").toDouble(), 0.0);
        QCOMPARE(field->mapToScene(QPointF()).y(), searchY);
        flick->setProperty("contentY", std::max(0.0, flick->property("contentHeight").toDouble() - home->height()));
        QTest::qWait(50);
        QVERIFY(window->grabWindow().save(evidence + "/bottom.png"));
        auto* searchShortcut = home->findChild<QObject*>("homeAnimeSearchShortcut");
        QVERIFY(searchShortcut);
        QVERIFY(QMetaObject::invokeMethod(searchShortcut, "activated"));
        QTRY_VERIFY(editor->hasActiveFocus());
        const auto fieldTop = field->mapToScene(QPointF()).y();
        QVERIFY(fieldTop >= -0.5);
        QVERIFY(fieldTop + field->height() <= titleBar->height());
        if (!live) {
            const auto submit = [&](const QString& text) {
                field->setProperty("text", text);
                editor->forceActiveFocus();
                if (text == "error") QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    searchIcon->mapToScene(QPointF(searchIcon->width() / 2, searchIcon->height() / 2)).toPoint());
                else QTest::keyClick(window, Qt::Key_Return);
            };
            submit("error");
            QTRY_COMPARE(flick->property("contentY").toDouble(), 0.0);
            QTRY_VERIFY(!metadata->searchError().isEmpty());
            auto* retry = find(home, "retryAnimeSearch");
            QVERIFY(retry && retry->isVisible());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, retry->mapToScene(QPointF(retry->width() / 2, retry->height() / 2)).toPoint());
            QTRY_VERIFY(!metadata->searching() && metadata->searchError().isEmpty() && !metadata->searchResults().isEmpty());
            submit("empty");
            QTRY_VERIFY(!metadata->searching());
            QVERIFY(metadata->searchResults().isEmpty());
            QVERIFY(metadata->searchError().isEmpty());
            QVERIFY(find(home, "homeSearchStatus")->property("text").toString().contains(QStringLiteral("没有找到")));
            submit("slow");
            QTRY_VERIFY(metadata->searching());
            QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 1)));
            QTest::qWait(260);
            QVERIFY(!metadata->searching());
            QVERIFY(metadata->searchKeyword().isEmpty());
            QVERIFY(metadata->searchResults().isEmpty());
            QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 0)));
            QVERIFY(field->property("text").toString().isEmpty());
        }
        editor->forceActiveFocus();
        field->setProperty("text", "clear me");
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(field->property("text").toString().isEmpty());
        QVERIFY(metadata->searchKeyword().isEmpty());
        QVERIFY(searchHint->isVisible());
        flick->setProperty("contentY", 0);
        QTRY_VERIFY(heading->isVisible());
        QTRY_VERIFY(field->mapToScene(QPointF(0, field->height())).y() <= home->mapToScene(QPointF()).y());
        QVERIFY(window->grabWindow().save(evidence + "/cleared-home.png"));
        field->setProperty("text", "clear button");
        QTest::qWait(20);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            clearIcon->mapToScene(QPointF(clearIcon->width() / 2, clearIcon->height() / 2)).toPoint());
        QTRY_VERIFY(field->property("text").toString().isEmpty());
        const auto headingY = heading->mapToScene(QPointF()).y();
        const auto headerHeight = titleBar->height();
        for (int pageIndex = 1; pageIndex < 5; ++pageIndex) {
            QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, pageIndex)));
            QTest::qWait(30);
            QCOMPARE(titleBar->height(), headerHeight);
            QVERIFY(!field->isVisible());
            QQuickItem* visibleHeading = nullptr;
            for (auto* candidate : window->findChildren<QQuickItem*>("pageHeading"))
                if (candidate->isVisible()) visibleHeading = candidate;
            QVERIFY(visibleHeading);
            QVERIFY(std::abs(visibleHeading->mapToScene(QPointF()).y() - headingY) < 0.5);
            QVERIFY(window->grabWindow().save(evidence + QString("/page-%1-heading.png").arg(pageIndex)));
        }
        QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 0)));
        for (const auto& signal : warnings) for (const auto& error : qvariant_cast<QList<QQmlError>>(signal.first())) loadErrors << error.toString();
        QVERIFY2(warnings.isEmpty(), qPrintable(loadErrors.join('\n')));
    }
    void opacityDragDuringMediaLossAndRecovery() { opacityDrag(false); }
    void realMediaSessionOpacityDrag() {
        if (qEnvironmentVariable("DANMAKU_TEST_MEDIA_SESSION").isEmpty())
            QSKIP("Opt in with DANMAKU_TEST_MEDIA_SESSION; never creates or controls a player session.");
        opacityDrag(true);
    }
    void homePageWorkflow() {
        QTemporaryDir directory;
        const auto cache = directory.filePath("cache");
        QVERIFY(QDir().mkpath(cache + "/metadata"));
        QImage poster(60, 90, QImage::Format_RGB32);
        poster.fill(QColor("#008080"));
        const auto posterPath = directory.filePath("poster.png");
        QVERIFY(poster.save(posterPath));
        const auto cover = QUrl::fromLocalFile(posterPath).toString();
        QJsonArray calendar;
        for (int day = 1; day <= 7; ++day) {
            calendar.append(QJsonObject{{"weekday", QJsonObject{{"id", day}}}, {"items", QJsonArray{
                QJsonObject{{"id", 123}, {"name_cn", "回归番剧"}, {"rating", QJsonObject{{"score", 8.2}}},
                    {"images", QJsonObject{{"large", cover}}}}}}});
        }
        QFile calendarFile(cache + "/metadata/calendar.json");
        QVERIFY(calendarFile.open(QIODevice::WriteOnly));
        calendarFile.write(QJsonDocument(calendar).toJson());
        calendarFile.close();
        auto cached = parseOnlineDanmaku(R"({"comments":[{"p":"1,1,16777215,user","m":"hello"}]})");
        cached.metadata = {{"server", "https://example.test"}, {"animeId", "42"}, {"episodeId", "42001"},
            {"animeTitle", "回归番剧"}, {"episodeTitle", "第1话"}, {"imageUrl", cover}, {"downloadedAt", "2026-10-14T00:00:00.000Z"}};
        QVERIFY(saveCachedDanmaku(cache + "/danmaku", cached).isEmpty());
        cached.metadata["episodeId"] = "42002";
        QVERIFY(saveCachedDanmaku(cache + "/danmaku", cached).isEmpty());
        QFile damaged(cache + "/danmaku/" + QString(64, 'f') + ".json");
        QVERIFY(damaged.open(QIODevice::WriteOnly)); damaged.write("broken"); damaged.close();

        QTcpServer server;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, &server, [&, socket, bytes] {
                    bytes->append(socket->readAll());
                    if (!bytes->contains("\r\n\r\n")) return;
                    disconnect(socket, &QTcpSocket::readyRead, &server, nullptr);
                    QByteArray body;
                    if (bytes->contains("/calendar")) body = QJsonDocument(calendar).toJson();
                    else if (bytes->contains("/v0/episodes")) body = R"({"data":[{"id":901,"sort":9,"name_cn":"第9话 冒险","type":0}],"total":1})";
                    else body = QJsonDocument(QJsonObject{{"id", 123}, {"name_cn", "回归番剧"},
                        {"summary", QStringLiteral("用于验证详情滚动与键盘操作的剧情简介。\n").repeated(40)},
                        {"images", QJsonObject{{"large", cover}}}}).toJson();
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        AppController backend(directory.filePath("data"), nullptr, {}, cache);
        auto* metadata = qobject_cast<AnimeMetadataService*>(backend.metadata());
        metadata->setBaseUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        auto* library = qobject_cast<DanmakuLibrary*>(backend.library());
        library->setServers({});
        QTRY_VERIFY(!library->scanning());
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        settings->setValue("micaEnabled", false);
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* home = window->findChild<QQuickItem*>("homePage");
        QVERIFY(home);
        QCOMPARE(home->property("recentWatchedList").toList().size(), 1);
        std::function<QQuickItem*(QQuickItem*, const QString&)> find = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto* child : parent->childItems()) if (auto* result = find(child, name)) return result;
            return nullptr;
        };
        QQuickItem* cachedCard = nullptr;
        QQuickItem* weeklyCard = nullptr;
        std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
            if (item->objectName() == "animeCard") {
                QCOMPARE(item->property("cardTitle").toString(), QStringLiteral("回归番剧"));
                QCOMPARE(item->property("cardCover").toString(), cover);
                if (!item->property("animeData").toMap().value("key").toString().isEmpty()) cachedCard = item;
                else weeklyCard = item;
            }
            for (auto* child : item->childItems()) collect(child);
        };
        QTest::qWait(100);
        collect(home);
        QVERIFY(cachedCard && weeklyCard);
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/home-fix/") +
            (qEnvironmentVariable("QT_QUICK_BACKEND") == "software" ? QStringLiteral("software") : QStringLiteral("native")) +
            (qEnvironmentVariableIsSet("DANMAKU_UI_LARGE_TEXT") ? QStringLiteral("-scaled") : QString());
        QDir().mkpath(evidence);
        for (const auto& theme : {"light", "dark"}) {
            settings->setValue("theme", theme);
            for (int width : {520, 800, 1080}) {
                window->resize(width, width == 520 ? 480 : 760);
                QTest::qWait(70);
                auto* weekday = find(home, "weekday7");
                QVERIFY(weekday);
                const auto right = weekday->mapToItem(home, QPointF(weekday->width(), 0)).x();
                QVERIFY2(right <= home->width(), qPrintable(QString("weekday right %1 > home width %2").arg(right).arg(home->width())));
                auto* flick = home->property("contentItem").value<QObject*>();
                QVERIFY(flick);
                flick->setProperty("contentY", 0);
                QVERIFY(window->grabWindow().save(evidence + QString("/home-%1-%2-top.png").arg(theme).arg(width)));
                flick->setProperty("contentY", std::max(0.0, flick->property("contentHeight").toDouble() - home->height()));
                QVERIFY(flick->property("contentY").toDouble() > 0);
                QVERIFY(window->grabWindow().save(evidence + QString("/home-%1-%2-bottom.png").arg(theme).arg(width)));
            }
        }
        auto* weekday = find(home, "weekday7");
        QVERIFY(weekday);
        weekday->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(home->property("activeWeekday").toInt(), 7);
        QVERIFY(QMetaObject::invokeMethod(cachedCard, "clicked"));
        QTRY_COMPARE(backend.total(), 1);
        QCOMPARE(window->property("currentPageName").toString(), QString("player"));
        auto* tabs = find(window->contentItem(), "danmakuSourceTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->property("currentIndex").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, 0)));
        metadata->fetchDetail(123);
        QTRY_VERIFY(!metadata->loadingDetail());
        auto* dialog = home->findChild<QObject*>("animeDetailDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(window->grabWindow().save(evidence + "/detail.png"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(metadata->currentDetail().isEmpty());
        metadata->fetchDetail(123);
        QTRY_VERIFY(!metadata->loadingDetail());
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto* content = dialog->property("contentItem").value<QQuickItem*>();
        QVERIFY(content);
        auto* episode = find(content, "animeEpisode9");
        QVERIFY(episode);
        auto* detailFlick = content->property("contentItem").value<QQuickItem*>();
        QVERIFY(detailFlick);
        const auto wheelPosition = content->mapToScene(QPointF(content->width() / 2, content->height() / 2));
        QWheelEvent scroll(wheelPosition, window->mapToGlobal(wheelPosition.toPoint()), {}, QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &scroll);
        QTRY_VERIFY(detailFlick->property("contentY").toDouble() > 0);
        detailFlick->setProperty("contentY", std::max(0.0,
            detailFlick->property("contentHeight").toDouble() - content->height()));
        QVERIFY(window->grabWindow().save(evidence + "/detail-bottom.png"));
        detailFlick->setProperty("contentY", 0);
        const auto originalDetail = metadata->currentDetail();
        auto datedDetail = originalDetail;
        datedDetail["episodes"] = QVariantList{
            QVariantMap{{"sort", 9}, {"airDate", "2026-10-15"}},
            QVariantMap{{"sort", 10}, {"airDate", "2026-10-16"}, {"title", "Future episode"}},
            QVariantMap{{"sort", 11}, {"airDate", "2026-02-30"}},
            QVariantMap{{"sort", 12}, {"airDate", ""}}};
        dialog->setProperty("currentDate", "2026-10-15");
        dialog->setProperty("detail", datedDetail);
        auto* futureEpisode = find(content, "animeEpisode10");
        QVERIFY(futureEpisode);
        QVERIFY(!futureEpisode->isEnabled());
        QVERIFY(find(content, "animeEpisode9")->isEnabled());
        QVERIFY(find(content, "animeEpisode11")->isEnabled());
        QVERIFY(find(content, "animeEpisode12")->isEnabled());
        QSignalSpy blockedSelection(dialog, SIGNAL(episodeSelected(QString,QString,double)));
        QVERIFY(blockedSelection.isValid());
        QTest::qWait(80);
        const auto futurePosition = futureEpisode->mapToScene(QPointF(futureEpisode->width() / 2, futureEpisode->height() / 2)).toPoint();
        struct CursorRestore { QPoint point; ~CursorRestore() { QCursor::setPos(point); } } restoreCursor{QCursor::pos()};
        QCursor::setPos(window->mapToGlobal(futurePosition));
        QTest::mouseMove(window, futurePosition);
        auto* futureHover = futureEpisode->parentItem()->findChild<QObject*>("animeEpisodeHover10");
        QVERIFY(futureHover);
        QTRY_VERIFY(futureHover->property("hovered").toBool());
        QQmlExpression tooltipVisible(qmlContext(futureEpisode->parentItem()), futureEpisode->parentItem(), "ToolTip.visible");
        QTRY_VERIFY(tooltipVisible.evaluate().toBool());
        QVERIFY(!tooltipVisible.hasError());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, futurePosition);
        QCOMPARE(blockedSelection.count(), 0);
        dialog->setProperty("currentDate", "2026-10-16");
        QVERIFY(futureEpisode->isEnabled());
        dialog->setProperty("detail", originalDetail);
        episode = find(content, "animeEpisode9");
        QVERIFY(episode);
        QSignalSpy selected(home, SIGNAL(openDanmakuForAnime(QString,QString,double)));
        QVERIFY(selected.isValid());
        episode->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected.first().at(1).toString(), QStringLiteral("第9话 冒险"));
        QCOMPARE(selected.first().at(2).toDouble(), 9.0);
        QCOMPARE(tabs->property("currentIndex").toInt(), 1);
        tabs->setProperty("currentIndex", 0);
        backend.showOnlineDanmakuForAnime(QStringLiteral("回归番剧"));
        QCOMPARE(tabs->property("currentIndex").toInt(), 1);
        QCOMPARE(warnings.count(), 0);
    }
    void toolbarSingleLineTips() {
        QTemporaryDir directory;
        AppController backend(directory.path(), nullptr, {}, directory.path(), [](QWindow*) { return true; });
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* panel = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        auto* toolbar = panel->findChild<QQuickWindow*>("danmakuToolbarWindow");
        auto* bar = toolbar->findChild<QQuickItem*>("danmakuToolbar");
        QVERIFY(panel && toolbar && bar);
        panel->show();
        backend.beginDemo(50);
        bar->setProperty("pinned", true);
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        auto tips = bar->findChildren<QObject*>("toolbarTip");
        tips.append(bar->findChild<QObject*>("toolbarPinTooltip"));
        QVERIFY(tips.size() >= 12);
        QStringList texts;
        for (auto* tip : tips) {
            QVERIFY(tip);
            texts.append(tip->property("text").toString());
        }
        QVERIFY(texts.contains(QStringLiteral("暂停")));
        QVERIFY(texts.contains(QStringLiteral("停止并退出")));
        QVERIFY(texts.contains(QStringLiteral("播放进度")));
        QVERIFY(texts.contains(QStringLiteral("一键清屏")));
        const auto originalScheme = qGuiApp->styleHints()->colorScheme();
#if defined(QT_DEBUG)
        const auto build = QStringLiteral("debug");
#else
        const auto build = QStringLiteral("release");
#endif
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/toolbar-coexist/") + build +
            QStringLiteral("-dpr%1").arg(toolbar->devicePixelRatio());
        QDir().mkpath(evidence);
        for (auto theme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
            qGuiApp->styleHints()->setColorScheme(theme);
            for (auto* tip : tips) {
                tip->setProperty("delay", 0);
                tip->setProperty("trigger", true);
                QTRY_VERIFY(tip->property("opened").toBool());
                auto* content = qobject_cast<QQuickItem*>(tip->property("contentItem").value<QObject*>());
                QVERIFY(content && content->window());
                QCOMPARE(content->property("lineCount").toInt(), 1);
                QCOMPARE(content->property("wrapMode").toInt(), 0); // Text.NoWrap
                QVERIFY(content->width() >= content->implicitWidth() - 0.5);
                QVERIFY(!content->property("truncated").toBool());
                if (tip->property("text").toString() == QStringLiteral("停止并退出")) {
                    QTest::qWait(30);
                    QVERIFY(content->window()->grabWindow().save(evidence +
                        (theme == Qt::ColorScheme::Light ? "/tip-light.png" : "/tip-dark.png")));
                }
                tip->setProperty("trigger", false);
                QTRY_VERIFY(!tip->property("visible").toBool());
            }
        }
        backend.stop(); backend.start(false);
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        auto* time = toolbar->findChild<QQuickItem*>("toolbarTimeButton");
        auto* offsetTip = time->findChild<QObject*>("toolbarTip");
        QVERIFY(offsetTip);
        QCOMPARE(offsetTip->property("text").toString(), QStringLiteral("重置偏移"));
        offsetTip->setProperty("trigger", true);
        QTRY_VERIFY(offsetTip->property("opened").toBool());
        auto* content = qobject_cast<QQuickItem*>(offsetTip->property("contentItem").value<QObject*>());
        QCOMPARE(content->property("lineCount").toInt(), 1);
        QTest::qWait(30);
        QVERIFY(content->window()->grabWindow().save(evidence + "/offset-tip.png"));
        backend.stop();
        QTRY_VERIFY(!offsetTip->property("visible").toBool());
        qGuiApp->styleHints()->setColorScheme(originalScheme);
        QCOMPARE(warnings.count(), 0);
    }

    void debugHudLongTitleLayout() {
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        QQmlComponent fixture(&engine);
        fixture.setData(R"(
            import QtQml
            QtObject {
                property QtObject settings: QtObject {
                    property var values: ({ debug: true, debugPosition: "top_left" })
                }
                property bool danmakuVisible: true
                property string status: "播放器已暂停"
                property string mediaTitle: ""
                property real position: 12345.6
                property var metrics: ({ active: 5000, dropped: 12345, presentedPerSecond: 165,
                    updatesPerSecond: 165, p95Ms: 123.4, memoryMiB: 2048.5,
                    textureEstimatedBytes: 536870912, textureBudgetBytes: 1073741824,
                    textFallbackNodes: 5000, textureFallbackReasons: { capacity: 12345 } })
            }
        )", QUrl());
        QVERIFY2(fixture.isReady(), qPrintable(fixture.errorString()));
        std::unique_ptr<QObject> backend(fixture.create());
        QVERIFY(backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/OverlayWindow.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"backend", QVariant::fromValue(backend.get())}}));
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        QVERIFY(window);
        auto* panel = window->findChild<QQuickItem*>("overlayDebugPanel");
        auto* title = window->findChild<QQuickItem*>("overlayDebugTitle");
        auto* status = window->findChild<QQuickItem*>("overlayDebugStatus");
        auto* metrics = window->findChild<QQuickItem*>("overlayDebugMetrics");
        QVERIFY(panel && title && status && metrics);
        window->resize(1024, 720);
        window->show();
        QTRY_VERIFY(window->isExposed());
        QTRY_COMPARE(panel->width(), 320.0);
        QCOMPARE(status->property("lineCount").toInt(), 1);
        QCOMPARE(metrics->property("lineCount").toInt(), 7);
        const auto completeMetrics = metrics->property("text").toString();
        QVERIFY(completeMetrics.contains("12345.6 s"));
        QVERIFY(completeMetrics.contains("512.0"));
        QVERIFY(completeMetrics.contains("1024"));
        QVERIFY(!metrics->property("truncated").toBool());
        const auto emptyHeight = panel->height();
        QVERIFY(!title->isVisible());
        for (const auto& value : {QStringLiteral("短标题"), QStringLiteral("长媒体标题，继续显示而不撑开调试面板。").repeated(50),
                QStringLiteral("https://example.test/") + QString(1000, 'W'),
                QString::fromUtf8("播放视频😀🎵测试").repeated(100)}) {
            QVERIFY(backend->setProperty("mediaTitle", value));
            QTRY_VERIFY(title->isVisible());
            QTRY_COMPARE(title->property("text").toString(), value);
            QCOMPARE(panel->width(), 320.0);
            QVERIFY(title->property("lineCount").toInt() <= 2);
            QCOMPARE(title->property("truncated").toBool(), value.size() > 100);
            QCOMPARE(metrics->property("text").toString(), completeMetrics);
            QCOMPARE(metrics->property("lineCount").toInt(), 7);
            QCOMPARE(backend->property("mediaTitle").toString(), value);
        }
        const auto literalTitle = QStringLiteral("<b>媒体标题</b>\r\n第二行\t片段\u2028尾部");
        QVERIFY(backend->setProperty("mediaTitle", literalTitle));
        QTRY_COMPARE(title->property("text").toString(), QStringLiteral("<b>媒体标题</b> 第二行 片段 尾部"));
        QCOMPARE(title->property("textFormat").toInt(), 0); // Text.PlainText
        QCOMPARE(backend->property("mediaTitle").toString(), literalTitle);
        QVERIFY(backend->setProperty("mediaTitle", "\r\n\t "));
        QTRY_VERIFY(!title->isVisible());
        QTRY_COMPARE(panel->height(), emptyHeight);

        auto* settings = backend->property("settings").value<QObject*>();
        QVERIFY(settings);
        QVERIFY(backend->setProperty("mediaTitle", QStringLiteral("超长标题").repeated(200)));
        for (const auto width : {1024, 240, 160}) {
            window->resize(width, 720);
            QTRY_COMPARE(panel->width(), double(std::min(320, width - 32)));
            QCOMPARE(metrics->property("text").toString(), completeMetrics);
            QVERIFY(!metrics->property("truncated").toBool());
            QVERIFY(title->property("lineCount").toInt() <= 2);
            for (const auto position : {"top_left", "top_right", "bottom_left", "bottom_right"}) {
                QVERIFY(settings->setProperty("values", QVariantMap{{"debug", true}, {"debugPosition", position}}));
                const QString corner = QString::fromLatin1(position);
                QTRY_COMPARE(panel->x(), corner.endsWith("right") ? window->width() - panel->width() - 16 : 16.0);
                QTRY_COMPARE(panel->y(), corner.startsWith("bottom") ? window->height() - panel->height() - 16 : 16.0);
                QVERIFY(panel->x() >= 0 && panel->y() >= 0);
                QVERIFY(panel->x() + panel->width() <= window->width());
                QVERIFY(panel->y() + panel->height() <= window->height());
            }
        }
        window->resize(1024, 720);
        QVERIFY(panel->setProperty("textPixelSize", 24.0));
        QTRY_VERIFY(qAbs(panel->width() - 320.0 * 24 / 14) < 0.01);
        QVERIFY(title->property("lineCount").toInt() <= 2);
        QCOMPARE(metrics->property("lineCount").toInt(), 7);
        QVERIFY(!metrics->property("truncated").toBool());
        QVERIFY(panel->setProperty("textPixelSize", 14.0));
        QTRY_COMPARE(panel->width(), 320.0);
#if defined(QT_DEBUG)
        const auto buildName = QStringLiteral("debug");
#else
        const auto buildName = QStringLiteral("release");
#endif
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/debug-hud/") + buildName +
                              QStringLiteral("-dpr%1").arg(window->devicePixelRatio());
        QDir().mkpath(evidence);
        QVERIFY(window->grabWindow().save(evidence + "/long-title.png"));
        const auto hudGrab = panel->grabToImage();
        QTRY_VERIFY(hudGrab && !hudGrab->image().isNull());
        QVERIFY(hudGrab->saveToFile(evidence + "/panel.png"));
        window->resize(240, 720);
        QTRY_COMPARE(panel->width(), 208.0);
        QVERIFY(window->grabWindow().save(evidence + "/narrow.png"));
        QVERIFY(window->flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(window->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QCOMPARE(warnings.count(), 0);
    }

    void nativeLayerOrderingWithoutActivation() {
#if defined(Q_OS_WIN)
        QQuickWindow panel, overlay, toolbar;
        panel.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
        panel.setGeometry(180, 180, 600, 400);
        panel.setColor(QColor("#303030"));
        overlay.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        overlay.setGeometry(panel.screen()->geometry());
        overlay.setColor(Qt::transparent);
        toolbar.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        toolbar.setGeometry(400, 0, 300, 48);
        toolbar.setColor(QColor("#404040"));
        overlay.setTransientParent(nullptr);
        toolbar.setTransientParent(&overlay);
        panel.show(); overlay.show(); toolbar.show();
        QTest::qWait(100);
        const auto foreground = GetForegroundWindow();
        const auto above = [](HWND upper, HWND lower) {
            for (auto current = GetWindow(lower, GW_HWNDPREV); current; current = GetWindow(current, GW_HWNDPREV))
                if (current == upper) return true;
            return false;
        };
        WindowLayerController layers;
        layers.attach(&panel, &overlay, &toolbar);
        const auto panelHandle = reinterpret_cast<HWND>(panel.winId());
        const auto overlayHandle = reinterpret_cast<HWND>(overlay.winId());
        for (int strategy = 0; strategy < 4; ++strategy) {
            overlay.setFlag(Qt::WindowStaysOnTopHint, strategy > 0);
            toolbar.setFlag(Qt::WindowStaysOnTopHint, strategy > 0);
            toolbar.show();
            QVERIFY(layers.reconcile(strategy, true, true, true));
            QVERIFY(above(reinterpret_cast<HWND>(toolbar.winId()), panelHandle));
            QVERIFY(above(panelHandle, overlayHandle));
            QCOMPARE(bool(GetWindowLongPtrW(panelHandle, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QCOMPARE(bool(GetWindowLongPtrW(overlayHandle, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QVERIFY(!panel.flags().testFlag(Qt::WindowStaysOnTopHint));
            QCOMPARE(GetForegroundWindow(), foreground);
            QVERIFY(layers.reconcile(strategy, false, true, true));
            QVERIFY(!(GetWindowLongPtrW(panelHandle, GWL_EXSTYLE) & WS_EX_TOPMOST));
        }
        WNDCLASSW probeClass{};
        probeClass.lpfnWndProc = DefWindowProcW;
        probeClass.hInstance = GetModuleHandleW(nullptr);
        probeClass.lpszClassName = L"DanmaXLayerNoActivateProbe";
        QVERIFY(RegisterClassW(&probeClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        struct NativeOwner {
            HWND window;
            ~NativeOwner() { if (IsWindow(window)) DestroyWindow(window); }
        } dialog{CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, probeClass.lpszClassName, L"Layer owner probe",
            WS_POPUP | WS_CAPTION, 240, 240, 300, 140, panelHandle, nullptr, probeClass.hInstance, nullptr)};
        QVERIFY(dialog.window);
        ShowWindow(dialog.window, SW_SHOWNOACTIVATE);
        NativeOwner nested{CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, probeClass.lpszClassName, L"Layer nested probe",
            WS_POPUP | WS_CAPTION, 280, 280, 240, 100, dialog.window, nullptr, probeClass.hInstance, nullptr)};
        QVERIFY(nested.window);
        ShowWindow(nested.window, SW_SHOWNOACTIVATE);
        QVERIFY(layers.reconcile(3, true, true, true));
        QVERIFY(above(nested.window, dialog.window));
        QVERIFY(above(dialog.window, panelHandle));
        QVERIFY(above(dialog.window, reinterpret_cast<HWND>(toolbar.winId())));
        QVERIFY(above(reinterpret_cast<HWND>(toolbar.winId()), panelHandle));
        QVERIFY(above(panelHandle, overlayHandle));
        QCOMPARE(GetForegroundWindow(), foreground);
        QVERIFY(layers.reconcile(3, false, false));
        QVERIFY(!(GetWindowLongPtrW(panelHandle, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QVERIFY(!(GetWindowLongPtrW(dialog.window, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QVERIFY(!(GetWindowLongPtrW(nested.window, GWL_EXSTYLE) & WS_EX_TOPMOST));
        DestroyWindow(nested.window);
        DestroyWindow(dialog.window);
        QVERIFY(layers.reconcile(3, true, true));
        panel.destroy();
        panel.create(); panel.show();
        QVERIFY(layers.reconcile(3, true, true));
        const auto recreated = reinterpret_cast<HWND>(panel.winId());
        QVERIFY(GetWindowLongPtrW(recreated, GWL_EXSTYLE) & WS_EX_TOPMOST);
        QVERIFY(above(recreated, overlayHandle));
        layers.attach(nullptr, nullptr, nullptr);
        QVERIFY(!(GetWindowLongPtrW(recreated, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QCOMPARE(GetForegroundWindow(), foreground);
#if defined(QT_DEBUG)
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/debug");
#else
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/release");
#endif
        QDir().mkpath(evidence);
        QFile report(evidence + "/native-order.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        const QJsonObject result{{"strategies", 4}, {"nestedOwners", true}, {"surfaceRecreated", true},
            {"baselineRestored", true}, {"foregroundPreserved", true}, {"foregroundPolicySimulated", true},
            {"toolbarAbovePanel", true}, {"dialogsAboveToolbar", true}};
        QVERIFY(report.write(QJsonDocument(result).toJson()) > 0);
#endif
    }

    void nativeToolbarMousePolicy() {
#if defined(Q_OS_WIN)
        QTemporaryDir directory;
        AppController backend(directory.path(), nullptr, {}, directory.path());
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* panel = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(panel);
        auto* overlay = panel->findChild<QQuickWindow*>("danmakuOverlay");
        auto* toolbar = panel->findChild<QQuickWindow*>("danmakuToolbarWindow");
        QVERIFY(overlay && toolbar);
        panel->showMinimized();
        QQuickWindow player;
        player.setFlags(Qt::Window | Qt::FramelessWindowHint);
        player.setGeometry(overlay->geometry());
        player.setColor(QColor("#303030"));
        player.show();
        player.requestActivate();
        QTest::qWait(200);
        const auto playerHandle = reinterpret_cast<HWND>(player.winId());
        if (GetForegroundWindow() != playerHandle)
            QSKIP("Windows foreground was not granted to the input fixture; no global mouse input was injected.");
        struct CursorRestore { QPoint point; ~CursorRestore() { QCursor::setPos(point); } } restore{QCursor::pos()};
        struct PressCounter final : QObject {
            int presses{};
            bool eventFilter(QObject*, QEvent* event) override {
                if (event->type() == QEvent::MouseButtonPress) ++presses;
                return false;
            }
        } counter;
        player.installEventFilter(&counter);
        QCursor::setPos(overlay->x() + 10, overlay->y() + 100);
        backend.beginDemo(50);
        QTRY_VERIFY(overlay->isExposed());
        QVERIFY(!toolbar->isVisible());
        POINT point{};
        GetCursorPos(&point);
        if (GetForegroundWindow() != playerHandle || WindowFromPoint(point) != playerHandle)
            QSKIP("The desktop input fixture is occluded; no global mouse input was injected.");
        INPUT inputs[2]{};
        inputs[0].type = inputs[1].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        QCOMPARE(SendInput(2, inputs, sizeof(INPUT)), UINT(2));
        QTRY_COMPARE(counter.presses, 1);
        auto* pointer = qobject_cast<ToolbarPointerMonitor*>(backend.toolbarPointer());
        QVERIFY(pointer);
        QTRY_VERIFY(pointer->activationRect().width() > 80);
        // Exercise the widened band through QCursor, beyond the old central 80px.
        for (const bool right : {false, true}) {
            const auto band = pointer->activationRect();
            QCursor::setPos(right ? band.right() - 2 : band.left() + 2, band.top() + 2);
            QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
            QCOMPARE(GetForegroundWindow(), playerHandle);
            if (!right) {
                QCursor::setPos(overlay->x() + 10, overlay->y() + 100);
                QTRY_COMPARE(toolbar->property("phase").toString(), QString("hidden"));
            }
        }
        const auto toolbarHandle = reinterpret_cast<HWND>(toolbar->winId());
        QVERIFY(GetForegroundWindow() != toolbarHandle);
        auto* button = toolbar->findChild<QQuickItem*>("toolbarOpacityButton");
        auto* popup = toolbar->findChild<QQuickItem*>("toolbarOpacityPopup");
        QVERIFY(button && popup);
        QCursor::setPos(toolbar->mapToGlobal(button->mapToScene(
            QPointF(button->width() / 2, button->height() / 2)).toPoint()));
        QTest::qWait(80);
        GetCursorPos(&point);
        if (GetForegroundWindow() != playerHandle || WindowFromPoint(point) != toolbarHandle)
            QSKIP("The toolbar input fixture is occluded; no toolbar mouse input was injected.");
        QCOMPARE(SendInput(2, inputs, sizeof(INPUT)), UINT(2));
        QTRY_VERIFY(popup->isVisible());
        QCOMPARE(GetForegroundWindow(), playerHandle);
        // New transport buttons use the same non-activating native host.
        auto* play = toolbar->findChild<QQuickItem*>("toolbarPlaybackButton");
        auto* exit = toolbar->findChild<QQuickItem*>("toolbarExitButton");
        QVERIFY(play && exit);
        auto expectedForeground = playerHandle;
        for (auto* control : {play, exit}) {
            QCursor::setPos(toolbar->mapToGlobal(control->mapToScene(
                QPointF(control->width() / 2, control->height() / 2)).toPoint()));
            QTest::qWait(80);
            GetCursorPos(&point);
            if (GetForegroundWindow() != expectedForeground || WindowFromPoint(point) != toolbarHandle)
                QSKIP("The transport button is occluded; no global mouse input was injected.");
            QCOMPARE(SendInput(2, inputs, sizeof(INPUT)), UINT(2));
            if (control == play) QTRY_VERIFY(!backend.playing());
            else QTRY_VERIFY(!backend.running());
            QCOMPARE(GetForegroundWindow(), expectedForeground);
            if (control == play) {
                toolbar->findChild<QQuickItem*>("danmakuToolbar")->setProperty("pinned", true);
                panel->show();
                panel->requestActivate();
                expectedForeground = reinterpret_cast<HWND>(panel->winId());
                SetForegroundWindow(expectedForeground);
                QTest::qWait(200);
                if (GetForegroundWindow() != expectedForeground)
                    QSKIP("Windows did not grant the panel foreground; coexistence mouse check not exercised.");
                QVERIFY(toolbar->isVisible());
                // The same physical click resumes while the panel stays foreground.
                QCursor::setPos(toolbar->mapToGlobal(play->mapToScene(
                    QPointF(play->width() / 2, play->height() / 2)).toPoint()));
                QTest::qWait(80);
                GetCursorPos(&point);
                QCOMPARE(WindowFromPoint(point), reinterpret_cast<HWND>(toolbar->winId()));
                QCOMPARE(SendInput(2, inputs, sizeof(INPUT)), UINT(2));
                QTRY_VERIFY(backend.playing());
                QCOMPARE(GetForegroundWindow(), expectedForeground);
                backend.showMainWindow();
                QTest::qWait(100);
                GetCursorPos(&point);
                QCOMPARE(WindowFromPoint(point), reinterpret_cast<HWND>(toolbar->winId()));
                QCOMPARE(GetForegroundWindow(), expectedForeground);
            }
        }
        QCOMPARE(backend.total(), 50);
#endif
    }

    void nativePanelAndDialogPolicy() {
#if defined(Q_OS_WIN)
        QTemporaryDir directory;
        AppController backend(directory.path(), nullptr, {}, directory.path());
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* panel = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(panel);
        auto* overlay = panel->findChild<QQuickWindow*>("danmakuOverlay");
        auto* toolbar = panel->findChild<QQuickWindow*>("danmakuToolbarWindow");
        QVERIFY(panel && overlay && toolbar);
        QVERIFY(!overlay->transientParent());
        QCOMPARE(toolbar->transientParent(), overlay);
        panel->show();
        panel->requestActivate();
        QTRY_VERIFY(panel->isActive());
        QTest::qWait(200);
        const auto owner = reinterpret_cast<HWND>(panel->winId());
        if (GetForegroundWindow() != owner)
            QSKIP("Windows foreground was not granted to the test panel; native owner policy needs an available desktop.");
        backend.beginDemo(50);
        QTRY_VERIFY(overlay->isExposed());
        QVERIFY(backend.toolbarPresentationAllowed());
        auto* bar = toolbar->findChild<QQuickItem*>("danmakuToolbar");
        QVERIFY(bar);
        bar->setProperty("pinned", true);
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        const auto nativeOverlay = reinterpret_cast<HWND>(overlay->winId());
        const auto above = [](HWND upper, HWND lower) {
            for (auto current = GetWindow(lower, GW_HWNDPREV); current; current = GetWindow(current, GW_HWNDPREV))
                if (current == upper) return true;
            return false;
        };
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        for (int strategy = 0; strategy < 4; ++strategy) {
            QVERIFY(settings->setValue("onTop", strategy));
            QTest::qWait(1100);
            QVERIFY(above(owner, nativeOverlay));
            QVERIFY(above(reinterpret_cast<HWND>(toolbar->winId()), owner));
            QCOMPARE(bool(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QCOMPARE(bool(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QVERIFY(!panel->flags().testFlag(Qt::WindowStaysOnTopHint));
            QCOMPARE(GetForegroundWindow(), owner);
        }
        panel->showMaximized();
        QTest::qWait(200);
        QVERIFY(toolbar->isVisible());
        QVERIFY(above(reinterpret_cast<HWND>(toolbar->winId()), owner));
        QCOMPARE(GetForegroundWindow(), owner);
        panel->showNormal();

        WNDCLASSW probeClass{};
        probeClass.lpfnWndProc = DefWindowProcW;
        probeClass.hInstance = GetModuleHandleW(nullptr);
        probeClass.lpszClassName = L"DanmaXToolbarOwnerProbe";
        QVERIFY(RegisterClassW(&probeClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        struct NativeDialog {
            HWND handle;
            ~NativeDialog() { if (handle) DestroyWindow(handle); }
        } dialog{CreateWindowExW(WS_EX_TOOLWINDOW, probeClass.lpszClassName, L"Toolbar policy native owner probe",
                               WS_POPUP | WS_CAPTION | WS_VISIBLE, 200, 200, 320, 160, owner, nullptr,
                               probeClass.hInstance, nullptr)};
        QVERIFY(dialog.handle);
        SetForegroundWindow(dialog.handle);
        QTRY_VERIFY(GetForegroundWindow() == dialog.handle);
        QVERIFY(MediaMonitor::windowOrOwnerForeground(panel->winId()));
        QTest::qWait(100);
        QVERIFY(backend.overlayVisible());
        QVERIFY(toolbar->isVisible());
        QVERIFY(above(dialog.handle, reinterpret_cast<HWND>(toolbar->winId())));
        QVERIFY(above(dialog.handle, owner));
        QVERIFY(above(owner, nativeOverlay));
        auto* tip = toolbar->findChild<QObject*>("toolbarPinTooltip");
        QVERIFY(tip);
        tip->setProperty("delay", 0);
        tip->setProperty("trigger", true);
        QTRY_VERIFY(tip->property("opened").toBool());
        auto* tipContent = qobject_cast<QQuickItem*>(tip->property("contentItem").value<QObject*>());
        QVERIFY(tipContent && tipContent->window());
        QTRY_VERIFY(above(dialog.handle, reinterpret_cast<HWND>(tipContent->window()->winId())));
        QCOMPARE(GetForegroundWindow(), dialog.handle);
        tip->setProperty("trigger", false);
        NativeDialog nested{CreateWindowExW(WS_EX_TOOLWINDOW, probeClass.lpszClassName, L"Nested owner probe",
            WS_POPUP | WS_CAPTION | WS_VISIBLE, 240, 240, 280, 120, dialog.handle, nullptr,
            probeClass.hInstance, nullptr)};
        QVERIFY(nested.handle);
        SetForegroundWindow(nested.handle);
        QTRY_VERIFY(GetForegroundWindow() == nested.handle);
        QTest::qWait(1100);
        QVERIFY(MediaMonitor::windowOrOwnerForeground(panel->winId()));
        QVERIFY(above(nested.handle, dialog.handle));
        QVERIFY(above(dialog.handle, reinterpret_cast<HWND>(toolbar->winId())));
        QVERIFY(above(dialog.handle, owner));
        QVERIFY(above(owner, nativeOverlay));
        ShowWindow(nested.handle, SW_HIDE);
        ShowWindow(dialog.handle, SW_HIDE);
        panel->showMinimized();
        QTRY_VERIFY(backend.overlayVisible());
        QTRY_VERIFY(backend.position() > 0);
        QTRY_VERIFY(!(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QVERIFY(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
        QVERIFY(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_NOACTIVATE);
        QVERIFY(!GetWindow(nativeOverlay, GW_OWNER));
        panel->showNormal();
        panel->requestActivate();
        SetForegroundWindow(owner);
        QTest::qWait(200);
        if (GetForegroundWindow() != owner)
            QSKIP("Windows did not regrant the panel foreground after minimization; maximized foreground check not exercised.");
        QTRY_VERIFY(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST);
#if defined(QT_DEBUG)
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/debug");
#else
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/release");
#endif
        QDir().mkpath(evidence);
        QVERIFY(panel->grabWindow().save(evidence + "/panel.png"));
        QVERIFY(overlay->grabWindow().save(evidence + "/overlay.png"));
        backend.writeReport(evidence + "/context.json");
        backend.stop();
        QVERIFY(!(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST));
#endif
    }

    void toolbarPlaybackWorkflow() {
        QTemporaryDir directory;
        QPoint cursor;
        bool panelOperating = false;
        AppController backend(directory.path(), nullptr, {}, directory.path(),
                              [&](QWindow*) { return panelOperating; }, [&] { return cursor; });
        QObject::disconnect(backend.findChild<MediaMonitor*>(), nullptr, &backend, nullptr);
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        QQmlComponent overlayComponent(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/OverlayWindow.qml")));
        QVERIFY2(overlayComponent.isReady(), qPrintable(overlayComponent.errorString()));
        std::unique_ptr<QObject> overlayObject(overlayComponent.createWithInitialProperties({{"backend", QVariant::fromValue(&backend)}}));
        auto* overlay = qobject_cast<QQuickWindow*>(overlayObject.get());
        QVERIFY(overlay);
        auto* renderer = qobject_cast<DanmakuItem*>(overlay->property("renderer").value<QObject*>());
        QQmlComponent toolbarComponent(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/ToolbarWindow.qml")));
        QVERIFY2(toolbarComponent.isReady(), qPrintable(toolbarComponent.errorString()));
        std::unique_ptr<QObject> toolbarObject(toolbarComponent.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&backend)}, {"overlayWindow", QVariant::fromValue(overlayObject.get())}}));
        auto* toolbar = qobject_cast<QQuickWindow*>(toolbarObject.get());
        QVERIFY(toolbar && renderer);
        QWindow panel;
        panel.show();
        backend.attach(renderer, overlay, toolbar, &panel);
        connect(&backend, &AppController::stateChanged, overlay,
                [&] { overlay->setVisible(backend.overlayVisible()); });
        auto find = [&](const char* name) { return toolbar->findChild<QQuickItem*>(name); };
        auto* bar = find("danmakuToolbar");
        auto* play = find("toolbarPlaybackButton");
        auto* exit = find("toolbarExitButton");
        auto* time = find("toolbarTimeButton");
        auto* seekPopup = find("toolbarSeekPopup");
        auto* seek = find("toolbarSeekSlider");
        auto* opacity = find("toolbarOpacityButton");
        auto* opacityPopup = find("toolbarOpacityPopup");
        QVERIFY(bar && play && exit && time && seekPopup && seek && opacity && opacityPopup);
        auto point = [](QQuickItem* item, double fraction = 0.5) {
            return item->mapToScene(QPointF(item->width() * fraction, item->height() / 2)).toPoint();
        };
        auto click = [&](QQuickItem* item) {
            cursor = toolbar->mapToGlobal(point(item));
            QTest::mouseClick(toolbar, Qt::LeftButton, Qt::NoModifier, point(item));
        };
        QSignalSpy loaded(&backend, &AppController::loadCompleted);
        backend.loadFile(QStringLiteral(DANMAKU_SOURCE_DIR "/tests/fixtures/sample.xml"));
        QTRY_COMPARE(loaded.count(), 1);
        backend.setManualMode(true);
        backend.start(true);
        bar->setProperty("pinned", true);
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        const int initialActivationWidth = backend.toolbarPointer()->property("activationWidth").toInt();
        QCOMPARE(initialActivationWidth, qRound(bar->property("activationWidth").toDouble()));
        QVERIFY(play->isVisible());
        click(play);
        QVERIFY(!backend.playing());
        backend.seek(20);
        click(find("toolbarSeekForwardButton"));
        QCOMPARE(backend.position(), 25.0);
        click(find("toolbarSeekBackButton"));
        QCOMPARE(backend.position(), 20.0);
        click(time);
        QTRY_VERIFY(seekPopup->isVisible());
        QCOMPARE(backend.toolbarPointer()->property("activationWidth").toInt(), initialActivationWidth);
        QVERIFY(!opacityPopup->isVisible());
        QTest::mousePress(toolbar, Qt::LeftButton, Qt::NoModifier, point(seek, 0.3));
        QTest::mouseMove(toolbar, point(seek, 0.7));
        QTRY_VERIFY(seek->property("pressed").toBool());
        QVERIFY(seek->property("pendingSeek").toBool());
        QCOMPARE(backend.position(), 20.0);
        bar->setProperty("pinned", false);
        cursor = overlay->geometry().center();
        QTest::qWait(1100);
        QVERIFY(toolbar->isVisible());
        const double target = seek->property("value").toDouble();
        QTest::mouseRelease(toolbar, Qt::LeftButton, Qt::NoModifier, point(seek, 0.7));
        QCOMPARE(backend.position(), target);
        QVERIFY(!backend.playing());
        bar->setProperty("pinned", true);
        click(opacity);
        QVERIFY(opacityPopup->isVisible());
        QCOMPARE(backend.toolbarPointer()->property("activationWidth").toInt(), initialActivationWidth);
        QVERIFY(!seekPopup->isVisible());
        click(time);
        QVERIFY(seekPopup->isVisible());
        QVERIFY(!opacityPopup->isVisible());
        const double beforeCancel = backend.position();
        QTest::mousePress(toolbar, Qt::LeftButton, Qt::NoModifier, point(seek, 0.2));
        QTest::mouseMove(toolbar, point(seek, 0.4));
        QTRY_VERIFY(seek->property("pressed").toBool());
        panelOperating = true;
        QTest::qWait(100);
        QVERIFY(backend.toolbarPresentationAllowed());
        QVERIFY(toolbar->isVisible());
        QVERIFY(seekPopup->isVisible());
        QVERIFY(seek->property("pendingSeek").toBool());
        // Only revoking presentation cancels a preview, not activating the panel.
        overlay->hide();
        QTRY_VERIFY(!toolbar->isVisible());
        QVERIFY(!seekPopup->isVisible());
        QTest::mouseRelease(toolbar, Qt::LeftButton, Qt::NoModifier, point(seek, 0.4));
        QCOMPARE(backend.position(), beforeCancel);
        QVERIFY(bar->property("pinned").toBool());
        overlay->show();
        panelOperating = false;
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        for (const auto theme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
            qGuiApp->styleHints()->setColorScheme(theme);
            for (const int width : {800, 320, 200}) {
                // AppController intentionally keeps the overlay at the selected
                // screen size. Exercise the same width input without resizing it.
                bar->setProperty("maximumWidth", width);
                QTest::qWait(80);
                QVERIFY(toolbar->width() <= width);
                auto* flow = find("toolbarFlow");
                QVERIFY(flow);
                auto* pointer = qobject_cast<ToolbarPointerMonitor*>(backend.toolbarPointer());
                QVERIFY(pointer);
                QCOMPARE(pointer->activationRect().width(), qRound(bar->property("activationWidth").toDouble()));
                const auto barLeft = toolbar->mapToGlobal(flow->parentItem()->mapToScene(QPointF(0, 0)).toPoint()).x();
                QVERIFY(qAbs(pointer->activationRect().left() - barLeft) <= 1);
                if (width <= 320) QVERIFY(flow->height() > 34);
                for (auto* control : {play, exit, time, opacity}) {
                    const auto scene = control->mapToScene(QPointF(0, 0));
                    QVERIFY(scene.x() >= 0 && scene.x() + control->width() <= toolbar->width() + 1);
                    QVERIFY(scene.y() >= 0 && scene.y() + control->height() <= toolbar->height() + 1);
                }
            }
        }
#if defined(QT_DEBUG)
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/playback-controls/debug") +
                              (qEnvironmentVariableIsSet("DANMAKU_UI_LARGE_TEXT") ? "-scaled" : "");
#else
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/playback-controls/release") +
                              (qEnvironmentVariableIsSet("DANMAKU_UI_LARGE_TEXT") ? "-scaled" : "");
#endif
        QDir().mkpath(evidence);
        QVERIFY(toolbar->grabWindow().save(evidence + "/toolbar-narrow.png"));
        bar->setProperty("maximumWidth", 1024);
        click(time);
        QTRY_VERIFY(seekPopup->isVisible());
        QTest::qWait(80);
        QVERIFY(toolbar->grabWindow().save(evidence + "/toolbar-progress.png"));
        const int manualActivationWidth = backend.toolbarPointer()->property("activationWidth").toInt();
        click(exit);
        QVERIFY(!backend.running());
        QVERIFY(backend.total() > 0);
        QTRY_VERIFY(!toolbar->isVisible());
        QVERIFY(!seekPopup->isVisible());
        backend.selectSession("fixture");
        backend.start(false);
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        QTRY_VERIFY(backend.toolbarPointer()->property("activationWidth").toInt() != manualActivationWidth);
        QCOMPARE(backend.toolbarPointer()->property("activationWidth").toInt(),
                 qRound(bar->property("activationWidth").toDouble()));
        QVERIFY(!play->isVisible());
        click(time);
        QVERIFY(!seekPopup->isVisible());
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        click(find("toolbarSeekForwardButton"));
        QCOMPARE(settings->values()["timeOffset"].toDouble(), 0.5);
        click(time);
        QCOMPARE(settings->values()["timeOffset"].toDouble(), 0.0);
        QTest::qWait(80);
        QVERIFY(toolbar->grabWindow().save(evidence + "/toolbar-follow.png"));
        click(exit);
        QVERIFY(!backend.running());
        QVERIFY(backend.total() > 0);
        backend.unload();
        QCOMPARE(warnings.count(), 0);
    }

    void toolbarAutoHideWorkflow() {
        QTemporaryDir directory;
        // Keep state/animation checks independent of concurrent desktop mouse movement.
        // nativeToolbarMousePolicy separately exercises QCursor and Windows hit testing.
        QPoint sensedCursor;
        bool panelOperating = false;
        AppController backend(directory.path(), nullptr, {}, directory.path(),
                              [&](QWindow*) { return panelOperating; }, [&] { return sensedCursor; });
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        QVERIFY(settings->setValue("onTop", 1));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        QQmlComponent overlayComponent(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/OverlayWindow.qml")));
        QVERIFY2(overlayComponent.isReady(), qPrintable(overlayComponent.errorString()));
        std::unique_ptr<QObject> overlayObject(overlayComponent.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&backend)}}));
        auto* overlayWindow = qobject_cast<QQuickWindow*>(overlayObject.get());
        QVERIFY(overlayWindow);
        auto& overlay = *overlayWindow;
        auto* canvas = qobject_cast<DanmakuItem*>(overlay.property("renderer").value<QObject*>());
        QVERIFY(canvas);
        auto& renderer = *canvas;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/ToolbarWindow.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&backend)}, {"overlayWindow", QVariant::fromValue(overlayObject.get())}}));
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        QVERIFY(window);
        auto* bar = window->findChild<QQuickItem*>("danmakuToolbar");
        auto* opacityButton = window->findChild<QQuickItem*>("toolbarOpacityButton");
        auto* pinButton = window->findChild<QQuickItem*>("toolbarPinButton");
        auto* pinTooltip = window->findChild<QObject*>("toolbarPinTooltip");
        auto* popup = window->findChild<QQuickItem*>("toolbarOpacityPopup");
        auto* slider = window->findChild<QQuickItem*>("toolbarOpacitySlider");
        QVERIFY(bar && opacityButton && pinButton && pinTooltip && popup && slider);
        QCOMPARE(window->transientParent(), &overlay);
        QWindow panel;
        panel.setGeometry(200, 150, 500, 400);
        panel.show();
        backend.attach(&renderer, &overlay, window, &panel);
        connect(&backend, &AppController::stateChanged, &overlay,
                [&] { overlay.setVisible(backend.overlayVisible()); });
        QQuickWindow player;
        player.setFlags(Qt::Window | Qt::FramelessWindowHint);
        player.setGeometry(overlay.geometry());
        player.setColor(QColor("#303030"));
        player.show();
        player.requestActivate();
        QTRY_VERIFY(player.isActive());
        const auto originalCursor = QCursor::pos();
        struct CursorRestore { QPoint point; ~CursorRestore() { QCursor::setPos(point); } } restore{originalCursor};
        auto outside = [&] { sensedCursor = overlay.geometry().center() + QPoint(0, 150); };
        auto enter = [&](bool right = false) {
            const auto rect = qobject_cast<ToolbarPointerMonitor*>(backend.toolbarPointer())->activationRect();
            sensedCursor = right ? rect.topRight() : rect.topLeft();
        };
        auto click = [&](QQuickItem* item) {
            QSignalSpy clicked(item, SIGNAL(clicked()));
            const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            sensedCursor = window->mapToGlobal(point);
            QCursor::setPos(sensedCursor);
            QTest::mouseMove(window, point);
            QTest::qWait(80);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_VERIFY(item->property("pressed").toBool());
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_COMPARE(clicked.count(), 1);
        };
        outside();
        backend.beginDemo(50);
        QTRY_VERIFY(overlay.isVisible());
        QTRY_VERIFY(backend.toolbarPointer()->property("enabled").toBool());
        QVERIFY(!window->isVisible());
        for (int cycle = 0; cycle < 3; ++cycle) {
            // Both ends are outside the previous 80-unit sensing strip.
            enter(cycle % 2 != 0);
            QTRY_VERIFY(backend.toolbarPointer()->property("hovered").toBool());
            QVERIFY(bar->property("presentationAllowed").toBool());
            QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
            QVERIFY(window->isVisible());
            const auto size = window->size();
            QVERIFY(size.width() > 80 && size.height() > 10);
            outside();
            QTRY_COMPARE(window->property("phase").toString(), QString("closing"));
            QCOMPARE(window->size(), size);
            QVERIFY(window->isVisible());
            if (cycle == 0) {
                enter();
                QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
                outside();
            } else if (cycle == 1) {
                panelOperating = true;
                QTest::qWait(50);
                QVERIFY(backend.toolbarPresentationAllowed());
                QVERIFY(backend.toolbarPointer()->property("enabled").toBool());
                panelOperating = false;
                QTRY_VERIFY(backend.toolbarPresentationAllowed());
            }
            QTRY_VERIFY(!window->isVisible());
            QCOMPARE(window->property("phase").toString(), QString("hidden"));
        }
        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        const auto pinPoint = pinButton->mapToScene(QPointF(pinButton->width() / 2, pinButton->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, pinPoint);
        QVERIFY(bar->property("interactionHeld").toBool());
        outside();
        // Keep the button press inside its hit area while the injected region
        // sensor is outside. Moving outside a Button cancels its press in Qt.
        QTest::qWait(1200);
        QVERIFY(bar->property("interactionHeld").toBool());
        QVERIFY(window->isVisible());
        panelOperating = true;
        QTest::qWait(100);
        QVERIFY2(window->isVisible(), qPrintable(QString("phase=%1 held=%2 pressed=%3 expanded=%4 allowed=%5")
            .arg(window->property("phase").toString()).arg(bar->property("interactionHeld").toBool())
            .arg(pinButton->property("pressed").toBool()).arg(bar->property("expanded").toBool())
            .arg(backend.toolbarPresentationAllowed())));
        QVERIFY(backend.toolbarPresentationAllowed());
        QVERIFY(overlay.isVisible());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(-100, 200));
        panelOperating = false;
        QTRY_VERIFY(backend.toolbarPresentationAllowed());
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(!bar->property("pinned").toBool());
        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(opacityButton);
        QVERIFY(popup->isVisible());
        // The transparent gap between card and popover must also retain the toolbar.
        sensedCursor = QPoint(window->x() + window->width() / 2, window->y() + 48);
        QTest::qWait(1200);
        QVERIFY(popup->isVisible());
        auto* handle = qobject_cast<QQuickItem*>(slider->property("handle").value<QObject*>());
        QVERIFY(handle);
        const auto thumb = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, thumb);
        QTRY_VERIFY(slider->property("pressed").toBool());
        panelOperating = true;
        QTest::qWait(100);
        QVERIFY(backend.toolbarPresentationAllowed());
        QVERIFY(overlay.isVisible());
        QVERIFY(window->isVisible());
        QVERIFY(popup->isVisible());
        overlay.hide();
        QVERIFY(!window->isVisible());
        QCOMPARE(window->property("phase").toString(), QString("hidden"));
        QVERIFY(!popup->isVisible());
        QVERIFY(!backend.toolbarPointer()->property("enabled").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, thumb);
        panelOperating = false;
        outside();
        overlay.show();
        QTRY_VERIFY(backend.toolbarPresentationAllowed());
        QVERIFY(!window->isVisible());
        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(opacityButton);
        QVERIFY(popup->isVisible());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, thumb);
        QTRY_VERIFY(slider->property("pressed").toBool());
        outside();
        QTest::mouseMove(window, QPoint(-100, 200));
        QTest::qWait(1200);
        QVERIFY(popup->isVisible());
        QVERIFY(window->isVisible());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(-100, 200));
        QTRY_VERIFY(!popup->isVisible());
        QTRY_VERIFY(!window->isVisible());

        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(pinButton);
        QVERIFY(bar->property("pinned").toBool());
        for (int strategy = 0; strategy < 4; ++strategy) {
            QVERIFY(settings->setValue("onTop", strategy));
            QTest::qWait(1100);
            QCOMPARE(overlay.flags().testFlag(Qt::WindowStaysOnTopHint), strategy > 0);
            QCOMPARE(window->flags().testFlag(Qt::WindowStaysOnTopHint), strategy > 0);
#if defined(Q_OS_WIN)
            const auto nativeToolbar = reinterpret_cast<HWND>(window->winId());
            const auto nativeOverlay = reinterpret_cast<HWND>(overlay.winId());
            QVERIFY(GetWindowLongPtrW(nativeToolbar, GWL_EXSTYLE) & WS_EX_NOACTIVATE);
            QCOMPARE(bool(GetWindowLongPtrW(nativeToolbar, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QCOMPARE(bool(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QVERIFY(GetWindow(nativeToolbar, GW_OWNER) == reinterpret_cast<HWND>(overlay.winId()));
#endif
        }
#if defined(QT_DEBUG)
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/toolbar/debug");
#else
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/toolbar/release");
#endif
        const auto scaledEvidence = evidence + QStringLiteral("-dpr%1").arg(overlay.devicePixelRatio());
        QDir().mkpath(scaledEvidence);
        QVERIFY(window->grabWindow().save(scaledEvidence + "/pinned.png"));
        backend.writeReport(scaledEvidence + "/context.json");
        click(opacityButton);
        QVERIFY(popup->isVisible());
        const auto scheme = QGuiApplication::styleHints()->colorScheme();
        for (auto theme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
            QGuiApplication::styleHints()->setColorScheme(theme);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(scaledEvidence +
                (theme == Qt::ColorScheme::Light ? "/popover-light.png" : "/popover-dark.png")));
        }
        QGuiApplication::styleHints()->setColorScheme(scheme);
        outside();
        QTRY_VERIFY(!popup->isVisible());
        QVERIFY(window->isVisible());
        QVERIFY(bar->property("expanded").toBool());
        auto hoverPinTooltip = [&] {
            const auto point = pinButton->mapToScene(QPointF(pinButton->width() / 2, pinButton->height() / 2)).toPoint();
            sensedCursor = window->mapToGlobal(point);
            QCursor::setPos(sensedCursor);
            QTest::mouseMove(window, point);
            QTRY_VERIFY(pinTooltip->property("opened").toBool());
            auto* content = qobject_cast<QQuickItem*>(pinTooltip->property("contentItem").value<QObject*>());
            QVERIFY(content && content->window() && content->window() != window);
            QCOMPARE(content->window()->type(), Qt::ToolTip);
            QVERIFY(!content->window()->isActive());
            const QRect buttonRect(sensedCursor - QPoint(qRound(pinButton->width() / 2), qRound(pinButton->height() / 2)),
                                   QSize(qRound(pinButton->width()), qRound(pinButton->height())));
            QVERIFY(!content->window()->geometry().intersects(buttonRect));
            QVERIFY(content->window()->grabWindow().save(scaledEvidence + "/pin-tooltip.png"));
        };
        hoverPinTooltip();
        panelOperating = true;
        QTest::qWait(100);
        QVERIFY(window->isVisible());
        QVERIFY(pinTooltip->property("visible").toBool());
        QVERIFY(bar->property("pinned").toBool());
        enter();
        QTest::qWait(1100);
        QVERIFY(window->isVisible());
        QVERIFY(overlay.isVisible());
        QVERIFY(backend.toolbarPointer()->property("enabled").toBool());
        outside();
        panelOperating = false;
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(opacityButton);
        QVERIFY(popup->isVisible());
        backend.stop();
        QTRY_VERIFY(!window->isVisible());
        QCOMPARE(window->property("phase").toString(), QString("hidden"));
        QVERIFY(!popup->isVisible());
        QVERIFY(!bar->property("autoExpanded").toBool());
        QVERIFY(bar->property("pinned").toBool());
        outside();
        backend.beginDemo(50);
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        hoverPinTooltip();
        click(pinButton);
        QVERIFY(!bar->property("pinned").toBool());
        outside();
        QTRY_VERIFY(!window->isVisible());
        backend.stop();
        QCOMPARE(warnings.count(), 0);
    }

    void backdropPolicyAndPersistence() {
        using Backdrop = WindowBackdropController;
        QVERIFY(Backdrop::fallback(true, true, true, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(false, true, true, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, false, true, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, false, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, true, true, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, true, false, false, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, true, false, true, true).isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        {
            SettingsStore settings(directory.path());
            QVERIFY(settings.values().value("micaEnabled").toBool());
            QVERIFY(settings.setValue("micaEnabled", false));
            QVERIFY(!settings.setValue("micaEnabled", "false"));
        }
        SettingsStore restored(directory.path());
        QVERIFY(!restored.values().value("micaEnabled").toBool());
        QVERIFY(restored.reset());
        QVERIFY(restored.values().value("micaEnabled").toBool());
    }
    void backdropSurfaceLifecycle() {
        QTemporaryDir directory;
        SettingsStore settings(directory.path());
        WindowBackdropController backdrop(&settings, nullptr);
        {
            QQuickWindow window;
            window.setColor(Qt::transparent);
            backdrop.attach(&window);
            // ui_workflow uses the software backend: no DWM mutation is allowed.
            QVERIFY(!backdrop.effectiveMica());
            QVERIFY(!backdrop.fallbackReason().isEmpty());
            window.destroy();
            QVERIFY(!backdrop.effectiveMica());
            window.create();
            QCoreApplication::processEvents();
            QVERIFY(!backdrop.effectiveMica());
            QVERIFY(settings.setValue("micaEnabled", false));
            // Destroy the target with a queued refresh outstanding.
        }
        QCoreApplication::processEvents();
        QVERIFY(!backdrop.effectiveMica());
    }
    void popupGeometryAndInput() {
        const QString output = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/scroll-popup")
            + qEnvironmentVariable("DANMAKU_UI_CAPTURE_SUFFIX");
        QDir().mkpath(output);
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls.FluentWinUI3
import "components"
ApplicationWindow {
    id: root
    visible: true
    width: 1080; height: 760
    property real anchorY: 40
    AppComboBox {
        id: choice; objectName: "probeChoice"
        x: 24; y: root.anchorY; width: root.width - 48
        model: []; textRole: "title"; valueRole: "id"
        Accessible.name: "剧集"
    }
    Button { x: 24; y: 0; text: "下一项" }
}
)", QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/PopupProbe.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* combo = window->findChild<QQuickItem*>("probeChoice");
        QVERIFY(combo);
        auto* popup = combo->property("popup").value<QObject*>();
        auto* list = qobject_cast<QQuickItem*>(combo->property("popupList").value<QObject*>());
        QVERIFY(popup && list);
        QSignalSpy hiding(popup, SIGNAL(aboutToHide()));
        QSignalSpy opened(popup, SIGNAL(opened()));
        auto* bar = qobject_cast<QQuickItem*>(combo->property("listScrollBar").value<QObject*>());
        QVERIFY(bar);
        auto bounds = [&] {
            const auto point = list->mapToScene(QPointF());
            QVERIFY(point.x() >= 7 && point.y() >= 7);
            QVERIFY(point.x() + list->width() <= window->width() - 7);
            QVERIFY(point.y() + list->height() <= window->height() - 7);
            QVERIFY(popup->property("height").toReal() <= combo->property("popupLimit").toReal() + 1);
        };
        QVariantList rows;
        for (int i = 0; i < 60; ++i)
            rows.append(QVariantMap{{"id", i}, {"title", QString::fromUtf8("第 %1 话 很长的中文剧集名称，用于验证文字省略和完整提示 Electro Master").arg(i + 1)}});
        combo->setProperty("model", rows);
        combo->setProperty("currentIndex", 59);
        // Isolate keyboard selection from synthetic/native pointer motion during
        // theme changes and high-DPI window repositioning.
        combo->setProperty("hoverEnabled", false);
        // Keep the pointer outside the popup while testing keyboard selection;
        // a stationary pointer over a row can legitimately change its highlight.
        QCursor::setPos(window->mapToGlobal(QPoint(1, 1)));
        QTest::mouseMove(window, QPoint(1, 1));
        QTest::qWait(50);
        combo->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(popup->property("opened").toBool());
        bounds();
        QVERIFY(bar->isVisible());
        QVERIFY(list->property("contentY").toReal() > 0);
        auto* current = qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());
        QVERIFY(current);
        QVERIFY(current->width() <= list->width());
        QVERIFY(current->y() + current->height() <= list->property("contentY").toReal() + list->height() + 1);
        QVERIFY(window->grabWindow().save(output + "/long-list.png"));
        auto* surface = popup->property("background").value<QObject*>();
        QVERIFY(surface);
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
        QTest::qWait(80);
        QVERIFY(window->grabWindow().save(output + "/long-list-light.png"));
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        QTest::qWait(80);
        QVERIFY(window->grabWindow().save(output + "/long-list-dark.png"));
        QCOMPARE(combo->property("highlightedIndex").toInt(), 59);
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(combo->property("highlightedIndex").toInt(), 58);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 58);
        QVERIFY(combo->hasActiveFocus());
        combo->setProperty("hoverEnabled", true);
        const auto openAfterResize = [&] {
            // Native resize/exposure and activation are asynchronous. Open only after
            // one frame of the resized window, with the same focus a user click supplies.
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(window));
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            combo->forceActiveFocus();
            QTRY_VERIFY(combo->hasActiveFocus());
            QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        };
        for (const int width : {1080, 520}) {
            window->resize(width, 480);
            window->setProperty("anchorY", 400);
            openAfterResize();
            QTRY_VERIFY2(popup->property("opened").toBool(), qPrintable(QString("width=%1 visible=%2 hides=%3 opens=%4 active=%5 height=%6")
                .arg(width).arg(popup->property("visible").toBool()).arg(hiding.count()).arg(opened.count())
                .arg(window->isActive()).arg(popup->property("height").toReal())));
            bounds();
            QVERIFY(popup->property("y").toReal() < 0);
            QVERIFY(window->grabWindow().save(output + QString("/upward-%1.png").arg(width)));
            window->resize(width, 320);
            QTest::qWait(80);
            bounds();
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!popup->property("visible").toBool());
        }
        window->resize(520, 480);
        window->setProperty("anchorY", 40);
        combo->setProperty("model", QVariantList{rows.first(), rows.at(1)});
        combo->setProperty("currentIndex", 0);
        openAfterResize();
        QTRY_VERIFY(popup->property("opened").toBool());
        bounds();
        QTRY_VERIFY(bar->property("size").toReal() >= 1.0);
        combo->setProperty("model", rows);
        QTRY_VERIFY(bar->property("size").toReal() < 1.0);
        bounds();
        // Drag the real standard scrollbar, then wheel the original popup ListView.
        const QPoint from = bar->mapToScene(QPointF(bar->width() / 2, bar->property("topPadding").toReal() + 3)).toPoint();
        const QPoint to = bar->mapToScene(QPointF(bar->width() / 2, bar->height() - bar->property("bottomPadding").toReal() - 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, to, 100);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_VERIFY(list->property("contentY").toReal() > 0);
        const qreal beforeWheel = list->property("contentY").toReal();
        const auto local = list->mapToScene(QPointF(40, 40));
        QWheelEvent wheel(local, window->mapToGlobal(local.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(list->property("contentY").toReal() < beforeWheel);
        combo->setProperty("model", QVariantList{});
        QTest::qWait(80);
        QTRY_COMPARE(list->property("contentHeight").toReal(), 0.0);
        QVERIFY(bar->height() <= 0 || bar->property("size").toReal() >= 1.0);
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(!combo->hasActiveFocus());
        QCOMPARE(warnings.count(), 0);
    }
    void workflow() {
        const QString output = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/ui-refresh")
            + qEnvironmentVariable("DANMAKU_UI_CAPTURE_SUFFIX");
        QDir().mkpath(output);
        QTemporaryDir temporary(output + "/session-XXXXXX");
        QVERIFY(temporary.isValid());
        AppController backend(temporary.path() + "/data", nullptr, {}, temporary.path() + "/cache");
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        auto* logs = qobject_cast<LogModel*>(backend.logs());
        settings->setValue("theme", "dark");
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // Repeater delegates belong to the visual tree, not necessarily the QObject tree.
        std::function<QQuickItem*(QQuickItem*, const QString&)> findVisual = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto* child : parent->childItems()) if (auto* match = findVisual(child, name)) return match;
            return nullptr;
        };
        auto find = [&](const char* name) { return findVisual(window->contentItem(), QString::fromLatin1(name)); };
        auto activate = [&](QQuickItem* item) {
            QVERIFY(item);
            item->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            QTest::qWait(50);
        };
        activate(find("navigation3"));
        QCOMPARE(window->property("selectedPage").toInt(), 3);
        activate(find("micaSwitch"));
        QVERIFY(!settings->values().value("micaEnabled").toBool());
        activate(find("micaSwitch"));
        QVERIFY(settings->values().value("micaEnabled").toBool());
        activate(find("navigation4"));
        QCOMPARE(window->property("selectedPage").toInt(), 4);
        QVERIFY(find("reportBugCard"));
        activate(find("navigation2"));
        QCOMPARE(window->property("selectedPage").toInt(), 2);
        activate(find("navigation0"));
        QCOMPARE(window->property("selectedPage").toInt(), 0);
        QVERIFY(find("homePage"));
        activate(find("navigation1"));
        QCOMPARE(window->property("selectedPage").toInt(), 1);
        auto* start = find("startPlayback");
        QVERIFY(start);
        auto* sourceTab = find("sourceLocalTab");
        QVERIFY(sourceTab);
        auto* tabLabel = sourceTab->property("contentItem").value<QObject*>();
        QVERIFY(tabLabel);
        QVERIFY(tabLabel->property("color").value<QColor>().alpha() > 0);
        QVERIFY(!start->isEnabled());
        QVERIFY(!find("unloadDanmaku")->isVisible());
        auto* disclosure = find("sessionDisclosure");
        QVERIFY(disclosure);
        disclosure->setProperty("expanded", true);
        find("sessionIdInput")->forceActiveFocus();
        disclosure->setProperty("expanded", false);
        auto* disclosureButton = qobject_cast<QQuickItem*>(disclosure->property("toggleButton").value<QObject*>());
        QVERIFY(disclosureButton && disclosureButton->hasActiveFocus());
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(output + "/empty-dark.png"));
        activate(find("manualMode"));
        QSignalSpy loaded(&backend, &AppController::loadCompleted);
        backend.loadFile(QStringLiteral(DANMAKU_SOURCE_DIR "/tests/fixtures/sample.xml"));
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        QVERIFY(backend.total() > 0);
        QTRY_VERIFY(start->isEnabled());
        activate(start);
        QVERIFY(backend.running());
        QVERIFY(backend.manualMode());
        QVERIFY(!find("manualMode")->isEnabled());
        activate(find("pausePlayback"));
        QVERIFY(!backend.playing());
        const auto paused = backend.position();
        QTest::qWait(80);
        QCOMPARE(backend.position(), paused);
        activate(find("pausePlayback"));
        QVERIFY(backend.playing());
        activate(find("stopPlayback"));
        QVERIFY(!backend.running());
        QVERIFY(backend.total() > 0);
        QVERIFY(backend.manualMode());
        QTRY_VERIFY(start->isEnabled());
        activate(start);
        QVERIFY(backend.running());
        backend.stop();
        activate(find("unloadDanmaku"));
        QCOMPARE(backend.total(), 0);
        activate(find("syncMode"));
        backend.selectSession("ui-test-session");
        loaded.clear();
        backend.loadFile(QStringLiteral(DANMAKU_SOURCE_DIR "/tests/fixtures/sample.xml"));
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        activate(start);
        QVERIFY(backend.running());
        QVERIFY(!backend.manualMode());
        QVERIFY(!find("pausePlayback")->isVisible());
        loaded.clear();
        backend.loadFile(output + "/does-not-exist.xml");
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        // Local XML loading intentionally unloads before preparation; preserve that contract.
        QCOMPARE(backend.total(), 0);
        QVERIFY(!backend.running());
        QVERIFY(!backend.error().isEmpty());
        backend.clearError();
        backend.stop();
        QFile emptyFile(temporary.filePath("empty.xml"));
        QVERIFY(emptyFile.open(QIODevice::WriteOnly));
        emptyFile.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?><i></i>");
        emptyFile.close();
        loaded.clear();
        backend.loadFile(emptyFile.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        QCOMPARE(backend.total(), 0);
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!start->isEnabled());
        backend.clearError();
        auto* tabs = find("danmakuSourceTabs");
        tabs->setProperty("currentIndex", 2);
        window->setProperty("selectedPage", 3);
        QTest::qWait(50);
        window->setProperty("selectedPage", 1);
        QCOMPARE(tabs->property("currentIndex").toInt(), 2);
        tabs->setProperty("currentIndex", 1);
        auto* player = tabs;
        while (player && player->metaObject()->indexOfSignal("openSettings()") < 0) player = player->parentItem();
        QVERIFY(player);
        QVERIFY(QMetaObject::invokeMethod(player, "openSettings"));
        QTRY_COMPARE(window->property("selectedPage").toInt(), 3);
        QTRY_VERIFY(find("danmakuServerInput"));
        QTRY_VERIFY(find("danmakuServerInput")->hasActiveFocus());
        const auto serverPosition = find("danmakuServerInput")->mapToScene(QPointF());
        QVERIFY(serverPosition.y() >= 0 && serverPosition.y() < window->height());
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(!find("danmakuServerInput")->hasActiveFocus());
        const auto defaults = settings->values()["danmakuServers"].toStringList();
        activate(find("addDanmakuServer"));
        QTRY_VERIFY(find("danmakuServerInput1"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList(), defaults);
        auto editServer = [&](const char* name, const QString& value) {
            auto* input = find(name); QVERIFY(input);
            input->setProperty("text", value);
            QVERIFY(QMetaObject::invokeMethod(input, "editingFinished"));
        };
        editServer("danmakuServerInput1", "bad address");
        QCOMPARE(find("danmakuServerInput1")->property("text").toString(), QString("bad address"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList(), defaults);
        editServer("danmakuServerInput1", defaults.first());
        QCOMPARE(settings->values()["danmakuServers"].toStringList(), defaults);
        editServer("danmakuServerInput1", "https://backup.test");
        activate(find("saveServersBtn"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().size(), 2);
        auto* onlineFrame = find("settingsPage");
        QVERIFY(onlineFrame);
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        for (const auto& theme : {QString("light"), QString("dark")}) {
            settings->setValue("theme", theme);
            window->resize(520, 640);
            QVERIFY(QMetaObject::invokeMethod(onlineFrame, "reveal", Q_ARG(QVariant, QVariant::fromValue(find("danmakuServerInput")))));
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(output + "/online-servers-" + theme + ".png"));
        }
        activate(find("serverUp1"));
        activate(find("saveServersBtn"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().first(), QString("https://backup.test"));
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        activate(find("serverDown0"));
        activate(find("saveServersBtn"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().first(), defaults.first());
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        activate(find("serverRemove1"));
        activate(find("serverRemove0"));
        activate(find("saveServersBtn"));
        QVERIFY(settings->values()["danmakuServers"].toStringList().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        QVERIFY(find("addDanmakuServer")->hasActiveFocus());
        QVERIFY(settings->setDanmakuServers(defaults));
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        QTRY_VERIFY(find("danmakuServerInput"));
        activate(find("cancelServersBtn"));
        window->setProperty("selectedPage", 2);
        logs->clear();
        QTRY_COMPARE(find("logEmptyState")->property("text").toString(), QString::fromUtf8("暂无日志"));
        logs->append("INFO", QString::fromUtf8("测试消息：这是一条用于检查换行和筛选状态的中文日志。"));
        find("logFilter")->setProperty("currentIndex", 2);
        QTRY_VERIFY(find("logEmptyState")->isVisible());
        QCOMPARE(find("logEmptyState")->property("text").toString(), QString::fromUtf8("当前筛选无结果"));
        logs->append("ERROR", "test error");
        QTRY_VERIFY(!find("logEmptyState")->isVisible());
        QCOMPARE(logs->countForLevel("ERROR"), 1);
        find("logFilter")->setProperty("currentIndex", 0);
        for (const auto& theme : {QString("light"), QString("dark")}) {
            settings->setValue("theme", theme);
            for (int width : {520, 800, 1080}) {
                window->resize(width, width == 520 ? 480 : 760);
                for (int page = 0; page < 4; ++page) {
                    QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, page)));
                    QTest::qWait(80);
                    QVERIFY(window->grabWindow().save(output + QString("/page-%1-%2-%3.png").arg(page).arg(theme).arg(width)));
                }
                auto* font = find("fontFamilyCombo");
                auto* frame = font->parentItem();
                while (frame && !frame->property("bodyItem").isValid()) frame = frame->parentItem();
                QVERIFY(frame);
                auto* body = qobject_cast<QQuickItem*>(frame->property("bodyItem").value<QObject*>());
                auto* bar = qobject_cast<QQuickItem*>(frame->property("verticalBar").value<QObject*>());
                auto* flick = frame->property("contentItem").value<QObject*>();
                QVERIFY(body && bar && flick);
                const auto bodyRight = body->mapToScene(QPointF(body->width(), 0)).x();
                const auto barLeft = bar->mapToScene(QPointF()).x();
                QVERIFY(bodyRight + 7 <= barLeft);
                QCOMPARE(qRound(frame->width() - bar->x() - bar->width()), 8);
                const auto bodyWidth = body->width();
                for (qreal position : {0.0, 0.4, 1.0}) {
                    bar->setProperty("position", std::min(position, 1 - bar->property("size").toReal()));
                    QTest::qWait(50);
                    QCOMPARE(body->width(), bodyWidth);
                    QVERIFY(flick->property("contentWidth").toReal() <= frame->width());
                    QVERIFY(window->grabWindow().save(output + QString("/scroll-%1-%2-%3.png").arg(theme).arg(width).arg(position)));
                }
                QVERIFY(flick->property("atYEnd").toBool());
                bar->setProperty("position", 0.0);
                QTest::qWait(50);
                const QPoint dragStart = bar->mapToScene(QPointF(bar->width() / 2, bar->property("topPadding").toReal() + 3)).toPoint();
                const QPoint dragEnd = bar->mapToScene(QPointF(bar->width() / 2, bar->height() - bar->property("bottomPadding").toReal() - 2)).toPoint();
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
                QTest::mouseMove(window, dragEnd, 80);
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
                QTRY_VERIFY(flick->property("atYEnd").toBool());
                QVERIFY(QMetaObject::invokeMethod(frame, "reveal", Q_ARG(QVariant, QVariant::fromValue(font))));
                font->forceActiveFocus();
                QTest::qWait(80);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    font->mapToScene(QPointF(font->width() - 16, font->height() / 2)).toPoint());
                auto* popup = font->property("popup").value<QObject*>();
                QTRY_VERIFY(popup->property("opened").toBool());
                QVERIFY(window->grabWindow().save(output + QString("/font-popup-%1-%2.png").arg(theme).arg(width)));
                QTest::keyClick(window, Qt::Key_Escape);
                QTRY_VERIFY(!popup->property("visible").toBool());
                font->forceActiveFocus();
                QTest::keyClick(window, Qt::Key_F4);
                QTRY_VERIFY(popup->property("opened").toBool());
                QTest::keyClick(window, Qt::Key_Escape);
                QTRY_VERIFY(!popup->property("visible").toBool());
                // Lower settings groups must be scrolled into view, beyond
                // initial page snapshots. Exercise the standard switch.
                for (const char* name : {"cacheGroup", "diagnosticsGroup"}) {
                    auto* group = find(name);
                    QVERIFY(group);
                    QTest::qWait(50);
                    QVERIFY(QMetaObject::invokeMethod(frame, "reveal", Q_ARG(QVariant, QVariant::fromValue(group))));
                    QTest::qWait(80);
                    group->setFocus(false);
                    QVERIFY(window->grabWindow().save(output + QString("/group-%1-%2-%3.png").arg(name).arg(theme).arg(width)));
                    if (QString::fromLatin1(name) == "diagnosticsGroup") {
                        const bool before = settings->values().value("debug").toBool();
                        activate(find("diagnosticsDebugSwitch"));
                        QCOMPARE(settings->values().value("debug").toBool(), !before);

                        settings->setValue("debug", before);
                    }
                }
                QVERIFY(QMetaObject::invokeMethod(frame, "reveal", Q_ARG(QVariant, QVariant::fromValue(font))));
            }
        }
        auto* font = find("fontFamilyCombo");
        auto* input = qobject_cast<QQuickItem*>(font->property("contentItem").value<QObject*>());
        QVERIFY(input);
        input->forceActiveFocus();
        const auto previousFamily = settings->values().value("fontFamily").toString();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const auto ch : QString("Segoe UI")) QTest::keyClick(window, ch.toLatin1());
        QCOMPARE(settings->values().value("fontFamily").toString(), previousFamily);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(settings->values().value("fontFamily").toString(), QString("Segoe UI"));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Backspace);
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(font->property("editText").toString(), QString("Segoe UI"));
        QCOMPARE(warnings.count(), 0);
        backend.stop();
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QFont font = app.font();
    font.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
    if (qEnvironmentVariableIsSet("DANMAKU_UI_LARGE_TEXT"))
        font.setPointSizeF(13.5);
    app.setFont(font);
    if (QFontDatabase::families().contains("Microsoft YaHei UI"))
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, "Microsoft YaHei UI");
    qmlRegisterType<DanmakuItem>("DanmaX.Native", 1, 0, "DanmakuCanvas");
    QWK::registerTypes(nullptr);
    UiWorkflowTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "UiWorkflowTests.moc"

