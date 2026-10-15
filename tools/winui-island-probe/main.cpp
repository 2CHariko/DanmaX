#include "IslandBridge.h"
#include <windows.h>
#undef GetCurrentTime
#include <psapi.h>
#include <MddBootstrap.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <Microsoft.UI.Dispatching.Interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QStyleHints>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCommandLineParser>
#include <algorithm>
#include <cstdio>

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace hosting = xaml::Hosting;
namespace dispatch = winrt::Microsoft::UI::Dispatching;
namespace {
QFile events;
QElapsedTimer startup;
void logEvent(const QString& event, QJsonObject fields = {}) {
    fields["event"] = event;
    fields["elapsedMs"] = startup.elapsed();
    fields["utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (events.isOpen()) { events.write(QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n'); events.flush(); }
}
void logMemory(const QString& stage) {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    QJsonObject modules;
    for (const auto* name : {L"Microsoft.ui.xaml.dll",L"Microsoft.UI.Xaml.Controls.dll",L"Microsoft.WindowsAppRuntime.dll",L"Microsoft.UI.Windowing.Core.dll"}) {
        wchar_t path[32768]{};
        if (const HMODULE module=GetModuleHandleW(name); module && GetModuleFileNameW(module,path,32768)) modules[QString::fromWCharArray(name)]=QString::fromWCharArray(path);
    }
    logEvent("memory",{{"stage",stage},{"privateBytes",static_cast<qint64>(memory.PrivateUsage)},
        {"workingSetBytes",static_cast<qint64>(memory.WorkingSetSize)},{"modules",modules}});
}
// No project-specific XAML/IDL compiler is required for this programmatic prototype.
// Forward built-in control metadata to the Microsoft provider.
struct ProbeApplication : xaml::ApplicationT<ProbeApplication, xaml::Markup::IXamlMetadataProvider> {
    ProbeApplication() {
        manager = hosting::WindowsXamlManager::InitializeForCurrentThread();
        Resources().MergedDictionaries().Append(controls::XamlControlsResources{});
    }
    xaml::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type) { return provider.GetXamlType(type); }
    xaml::Markup::IXamlType GetXamlType(winrt::hstring const& type) { return provider.GetXamlType(type); }
    winrt::com_array<xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions() { return provider.GetXmlnsDefinitions(); }
    void OnLaunched(xaml::LaunchActivatedEventArgs const&) { }
    xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
    hosting::WindowsXamlManager manager{nullptr};
};
}

struct IslandBridge::State {
    QQuickWindow* window = nullptr;
    QQuickItem* slot = nullptr;
    hosting::DesktopWindowXamlSource source{nullptr};
    controls::StackPanel panel{nullptr};
    controls::ComboBox shortCombo{nullptr}, longCombo{nullptr}, editableCombo{nullptr};
    winrt::event_token changed{}, leave{}, opened{}, closed{}, edited{};
    QString selection, status = "Not attached";
    int index = -1, generation = 0;
    bool dark = false;
    QRect geometry;
};
IslandBridge::IslandBridge(QObject* parent) : QObject(parent), s(std::make_unique<State>()) { }
IslandBridge::~IslandBridge() { close(); }
int IslandBridge::selectedIndex() const { return s->index; }
QString IslandBridge::selection() const { return s->selection; }
QString IslandBridge::status() const { return s->status; }
void IslandBridge::report(const QString& event) { logEvent(event); }
void IslandBridge::attach(QQuickWindow* window, QQuickItem* slot) {
    s->window = window; s->slot = slot;
    s->source = hosting::DesktopWindowXamlSource{};
    s->source.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(reinterpret_cast<HWND>(window->winId())));
    s->panel = xaml::Markup::XamlReader::Load(L"<StackPanel xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource ApplicationPageBackgroundThemeBrush}'/>").as<controls::StackPanel>();
    s->panel.Spacing(8);
    s->panel.Padding({12, 8, 12, 8});
    const auto makeCombo = [&](const wchar_t* label) {
        controls::ComboBox combo;
        combo.Header(winrt::box_value(label));
        combo.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
        combo.MaxDropDownHeight(360);
        xaml::Automation::AutomationProperties::SetName(combo, label);
        s->panel.Children().Append(combo);
        return combo;
    };
    s->shortCombo = makeCombo(L"Native WinUI / 原生短列表");
    for (const auto* text : {L"System / 系统", L"Light / 浅色", L"Dark / 深色"}) s->shortCombo.Items().Append(winrt::box_value(text));
    s->shortCombo.SelectedIndex(0);
    s->longCombo = makeCombo(L"Native WinUI / 原生长列表 (120 items)");
    for (int i = 0; i < 120; ++i) s->longCombo.Items().Append(winrt::box_value(winrt::hstring(L"Option / 选项 ") + winrt::to_hstring(i)));
    s->longCombo.SelectedIndex(119);
    s->editableCombo = makeCombo(L"Native editable / 原生可编辑");
    s->editableCombo.IsEditable(true);
    for (const auto* font : {L"Segoe UI", L"Microsoft YaHei UI", L"Consolas"}) s->editableCombo.Items().Append(winrt::box_value(font));
    s->editableCombo.SelectedIndex(0);
    s->changed = s->longCombo.SelectionChanged([this](auto const&, auto const&) {
        s->index = s->longCombo.SelectedIndex();
        s->selection = QString::fromStdWString(winrt::unbox_value_or<winrt::hstring>(s->longCombo.SelectedItem(), L"").c_str());
        logEvent("native-selection", {{"index",s->index},{"text",s->selection}});
        Q_EMIT selectionChanged();
    });
    s->opened = s->longCombo.DropDownOpened([](auto const&, auto const&) { logEvent("native-dropdown-opened"); });
    s->closed = s->longCombo.DropDownClosed([](auto const&, auto const&) { logEvent("native-dropdown-closed"); });
    s->edited = s->editableCombo.TextSubmitted([](auto const&, auto const& args) { logEvent("native-text-submitted", {{"text",QString::fromStdWString(args.Text().c_str())}}); });
    s->leave = s->source.TakeFocusRequested([this](auto const&, auto const& args) {
        const bool reverse = args.Request().Reason() == hosting::XamlSourceFocusNavigationReason::Last;
        logEvent("native-leave-focus", {{"reverse",reverse}});
        Q_EMIT leaveNative(reverse);
    });
    s->source.Content(s->panel);
    setDark(s->dark);
    const HWND child = winrt::Microsoft::UI::GetWindowFromWindowId(s->source.SiteBridge().WindowId());
    SetWindowLongPtrW(child, GWL_STYLE, GetWindowLongPtrW(child, GWL_STYLE) | WS_TABSTOP);
    s->status = QString("Attached real WinUI 3 / generation %1 / native HWND %2").arg(++s->generation).arg(reinterpret_cast<quintptr>(child),0,16);
    s->geometry = {};
    syncGeometry();
    logEvent("island-attached", {{"generation",s->generation},{"nativeClass","Microsoft.UI.Xaml.Controls.ComboBox"}});
    Q_EMIT statusChanged();
}
void IslandBridge::close() {
    if (!s->source) return;
    if (s->longCombo) {
        s->longCombo.IsDropDownOpen(false);
        if (s->changed.value) s->longCombo.SelectionChanged(s->changed);
        if (s->opened.value) s->longCombo.DropDownOpened(s->opened);
        if (s->closed.value) s->longCombo.DropDownClosed(s->closed);
    }
    if (s->editableCombo) {
        s->editableCombo.IsDropDownOpen(false);
        if (s->edited.value) s->editableCombo.TextSubmitted(s->edited);
    }
    if (s->shortCombo) s->shortCombo.IsDropDownOpen(false);
    if (s->leave.value) s->source.TakeFocusRequested(s->leave);
    s->source.Content(nullptr);
    s->shortCombo = nullptr; s->longCombo = nullptr; s->editableCombo = nullptr; s->panel = nullptr;
    s->source.Close(); s->source = nullptr;
    s->changed={}; s->opened={}; s->closed={}; s->edited={}; s->leave={};
    logEvent("island-closed");
}
void IslandBridge::syncGeometry() {
    if (!s->source || !s->window || !s->slot) return;
    const QPointF point = s->slot->mapToScene(QPointF());
    const qreal dpr = s->window->devicePixelRatio();
    const QRect pixels(qRound(point.x()*dpr),qRound(point.y()*dpr),qRound(s->slot->width()*dpr),qRound(s->slot->height()*dpr));
    if (pixels != s->geometry) {
        s->geometry = pixels;
        s->source.SiteBridge().MoveAndResize({pixels.x(),pixels.y(),pixels.width(),pixels.height()});
        logEvent("native-geometry", {{"x",pixels.x()},{"y",pixels.y()},{"width",pixels.width()},{"height",pixels.height()},{"dpr",dpr}});
    }
}
void IslandBridge::setDark(bool value) {
    s->dark = value;
    QGuiApplication::styleHints()->setColorScheme(value ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    if (s->panel) s->panel.RequestedTheme(value ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light);
    logEvent("theme", {{"dark",value}});
}
void IslandBridge::focusNative(bool reverse) {
    if (!s->source) return;
    s->source.NavigateFocus(hosting::XamlSourceFocusNavigationRequest(reverse ? hosting::XamlSourceFocusNavigationReason::Last : hosting::XamlSourceFocusNavigationReason::First));
    logEvent("native-enter-focus", {{"reverse",reverse}});
}
void IslandBridge::focusQt() {
    if (s->window) SetFocus(reinterpret_cast<HWND>(s->window->winId()));
    logEvent("qt-enter-focus");
}
void IslandBridge::selectLast() { if (s->longCombo) s->longCombo.SelectedIndex(119); }
void IslandBridge::setPopupOpen(bool value) { if (s->longCombo) s->longCombo.IsDropDownOpen(value); }
void IslandBridge::recreate() { auto* window=s->window; auto* slot=s->slot; close(); attach(window,slot); }
bool IslandBridge::smokeCheck() {
    if (!s->source) return false;
    s->longCombo.SelectedIndex(5);
    const bool forward = s->index == 5 && s->selection.contains("5");
    s->longCombo.SelectedIndex(119);
    const bool backward = s->index == 119;
    setPopupOpen(true); const bool opened=s->longCombo.IsDropDownOpen(); setPopupOpen(false);
    setDark(true); setDark(false);
    for (int i=0;i<10;++i) recreate();
    logEvent("smoke-check", {{"selectionToQt",forward},{"selectionFromQt",backward},{"popupProperty",opened},{"recreated",10}});
    return forward && backward && opened;
}
bool IslandBridge::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
    // Qt's windows_generic_MSG is delivered just before TranslateMessage/DispatchMessage.
    // Non-null result is the later WndProc hook; do not pre-translate twice.
    if (s->source && !result && type == "windows_generic_MSG" && ContentPreTranslateMessage(static_cast<MSG*>(message))) {
        return true;
    }
    return false;
}

int runQuickWindow(QGuiApplication& app, const QCommandLineParser& parser, bool native) {
    IslandBridge bridge;
    QQmlApplicationEngine engine;
    bool smokePassed=true;
    engine.rootContext()->setContextProperty("bridge",&bridge);
    QObject::connect(&engine,&QQmlApplicationEngine::warnings,&app,[](auto const& warnings) { for (auto const& warning:warnings) logEvent("qml-warning",{{"message",warning.toString()}}); });
    engine.loadFromModule("WinUIProbe","Main");
    if (engine.rootObjects().isEmpty()) throw std::runtime_error("QML load failed");
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* slot=window->findChild<QQuickItem*>("islandSlot");
    if (!slot) throw std::runtime_error("Island slot missing");
    if (native) {
        bridge.attach(window,slot);
        app.installNativeEventFilter(&bridge);
        QObject::connect(window,&QWindow::widthChanged,&bridge,&IslandBridge::syncGeometry);
        QObject::connect(window,&QWindow::heightChanged,&bridge,&IslandBridge::syncGeometry);
        QObject::connect(window,&QWindow::screenChanged,&bridge,&IslandBridge::syncGeometry);
        // Close the island BEFORE Qt destroys its parent HWND, including title-bar closes.
        QObject::connect(window,&QQuickWindow::closing,&bridge,[&] { bridge.report("qt-window-closing"); bridge.close(); });
    }
    logMemory(native ? "native-attached" : "qt-only-loaded");
    QTimer::singleShot(800,&bridge,[&,window] {
        logEvent("ready",{{"native",native},{"dpr",window->devicePixelRatio()},{"windowWidth",window->width()},{"windowHeight",window->height()}});
        logMemory("ready");
        QMetaObject::invokeMethod(window,"scenario",Q_ARG(QVariant,QVariant(parser.value("scenario"))));
        if (parser.isSet("smoke-test")) {
            smokePassed=!native || bridge.smokeCheck();
            window->close();
        }
    });
    const int result=app.exec();
    app.removeNativeEventFilter(&bridge);
    bridge.close();
    return smokePassed ? result : 3;
}

int main(int argc, char** argv) {
    startup.start();
    wchar_t executablePath[32768]{};
    const DWORD executableLength=GetModuleFileNameW(nullptr,executablePath,32768);
    if (!executableLength || executableLength>=32768) return 2;
    const QString configuredCache=qEnvironmentVariable("WINUI_PROBE_CACHE_ROOT");
    const QString cache=configuredCache.isEmpty()
        ? QDir(QFileInfo(QString::fromWCharArray(executablePath)).absolutePath()).filePath("cache")
        : QFileInfo(configuredCache).absoluteFilePath();
    if (!QDir().mkpath(QDir(cache).filePath("tmp")) || !QDir().mkpath(QDir(cache).filePath("qml"))) return 2;
    qputenv("TEMP",QDir(cache).filePath("tmp").toUtf8());
    qputenv("TMP",QDir(cache).filePath("tmp").toUtf8());
    qputenv("QML_DISK_CACHE_PATH",QDir(cache).filePath("qml").toUtf8());
    qputenv("QT_DISABLE_SHADER_DISK_CACHE","1");
    qunsetenv("QSG_RHI_PIPELINE_CACHE_LOAD");
    qunsetenv("QSG_RHI_PIPELINE_CACHE_SAVE");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"report-dir","Write event/metrics evidence","directory"});
    parser.addOption({"smoke-test","Exercise native selection, popup and ten island recreations, then exit"});
    parser.addOption({"qt-only","Load the same Qt page without initializing Windows App SDK (cost baseline)"});
    parser.addOption({"self-contained","Load an app-local Windows App Runtime; no bootstrap or installed runtime"});
    parser.addOption({"scenario","Interactive scenario: baseline, popup, modal, clip","name","baseline"});
    parser.process(app);
    const QString directory=parser.value("report-dir");
    if (directory.isEmpty() || !QDir().mkpath(directory)) return 2;
    events.setFileName(QDir(directory).filePath("events.jsonl"));
    if (!events.open(QIODevice::WriteOnly|QIODevice::Truncate)) return 2;
    logEvent("qt-started",{{"qt",qVersion()},{"cache",cache},{"pid",static_cast<qint64>(GetCurrentProcessId())}});
    bool bootstrapped=false;
    int exitCode=1;
    try {
        if (parser.isSet("qt-only")) {
            exitCode=runQuickWindow(app,parser,false);
            logEvent("exit",{{"code",exitCode}});
            return exitCode;
        }
        // Existing 1.8 stable runtime, explicit bootstrap; never install or fall back to another version.
        if (parser.isSet("self-contained")) {
            const auto path=QDir(app.applicationDirPath()).filePath("Microsoft.WindowsAppRuntime.dll").toStdWString();
            const HMODULE runtime=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!runtime) winrt::throw_last_error();
            const auto ensure=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(runtime,"WindowsAppRuntime_EnsureIsLoaded"));
            if (!ensure) winrt::throw_last_error();
            winrt::check_hresult(ensure());
            logEvent("app-local-runtime-loaded");
        } else {
            PACKAGE_VERSION minVersion{}; minVersion.Major=8000;
            winrt::check_hresult(MddBootstrapInitialize2(0x00010008,L"",minVersion,MddBootstrapInitializeOptions_None));
            bootstrapped=true;
            logEvent("bootstrap-ok");
        }
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        {
            const auto queue=dispatch::DispatcherQueueController::CreateOnCurrentThread();
            {
                auto xamlApp=winrt::make_self<ProbeApplication>();
                exitCode=runQuickWindow(app,parser,true);
                xamlApp->manager.Close();
                logEvent("xaml-manager-closed");
                xamlApp=nullptr;
            }
            winrt::clear_factory_cache();
            queue.ShutdownQueue();
            logEvent("dispatcher-shutdown");
        }
        winrt::uninit_apartment();
    } catch (winrt::hresult_error const& error) {
        logEvent("winrt-error",{{"hresult",QString::number(static_cast<quint32>(error.code().value),16)},{"message",QString::fromStdWString(error.message().c_str())}});
    } catch (std::exception const& error) { logEvent("error",{{"message",error.what()}}); }
    if (bootstrapped) MddBootstrapShutdown();
    logMemory("after-shutdown");
    logEvent("exit",{{"code",exitCode}});
    return exitCode;
}
