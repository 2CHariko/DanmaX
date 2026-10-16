#pragma once
#include "platform/windows/MediaMonitor.h"
#include <chrono>
#include <functional>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

struct SmtcPoll {
    MediaSample sample;
    QVariantList sessions;
    QString error;
    bool publishSessions{};
    bool cancelled{};
};

// Created, used and destroyed in the media worker's initialized WinRT apartment.
class SmtcReader {
  public:
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    using ManagerRequest = std::function<winrt::Windows::Foundation::IAsyncOperation<Manager>()>;
    explicit SmtcReader(ManagerRequest request = {});
    SmtcPoll poll(const QString& selected, quint64 generation, std::stop_token stop = {});
    QString control(const MediaSample& expected, bool play, std::stop_token stop = {}, std::function<bool()> current = {});

  private:
    ManagerRequest request_;
    Manager manager_{nullptr};
    std::chrono::steady_clock::time_point reconnectAt_{};
    int cycle_{};
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession selectedSession_{nullptr};
    quint64 sessionToken_{};
};
