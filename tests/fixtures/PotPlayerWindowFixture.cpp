#include <windows.h>
#include <string>
#include <shellapi.h>

namespace {
double position{}, rate{1.5}; ULONGLONG stamp{};
bool playing{true}, stall{}, refuseClose{};
double current() { return position + (playing ? (GetTickCount64() - stamp) * rate : 0); }
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM command, LPARAM value) {
    if (message == WM_USER) {
        if (command == 0x5ff0) { stall = value != 0; return 1; }
        if (command == 0x5ff1) { refuseClose = value != 0; return 1; }
        if (stall && command >= 0x5002 && command <= 0x5007) Sleep(250);
        switch (command) {
        case 0x5002: return 120000;
        case 0x5004: return LRESULT(current());
        case 0x5006: return playing ? 2 : 1;
        case 0x5005: position = double(value); stamp = GetTickCount64(); return 0;
        case 0x5007: position = current(); stamp = GetTickCount64(); playing = value == 2; return 0;
        }
    }
    if (message == WM_CLOSE) { if (!refuseClose) DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, command, value);
}
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    int count{}; auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments || count < 2) return 2;
    std::wstring title(arguments[1]); LocalFree(arguments);
    const auto at = title.find_last_of(L"/\\"); if (at != std::wstring::npos) title = title.substr(at + 1);
    if (title == L"startup-delay.avi") Sleep(500);
    WNDCLASSW cls{}; cls.lpfnWndProc = procedure; cls.hInstance = instance; cls.lpszClassName = L"PotPlayer64";
    if (!RegisterClassW(&cls)) return 2;
    stamp = GetTickCount64();
    const auto window = CreateWindowW(cls.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, 20, 20, 320, 180,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    // No visible UI: this fixture measures real Win32 IPC and process lifetime,
    // never pretends to decode video or substitute for a real-player run.
    MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
