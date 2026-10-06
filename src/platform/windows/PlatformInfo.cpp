#include "platform/windows/PlatformInfo.h"
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>

QString windowsPlatformDescription() {
    // Compile/link probe only; this does not enumerate or claim a live media session.
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    const auto className = winrt::name_of<Manager>();
    return QStringLiteral("Windows x64 · C++/WinRT SDK 可用 · SMTC 尚未接入")
        + (className.empty() ? QStringLiteral(" (metadata unavailable)") : QString{});
}
