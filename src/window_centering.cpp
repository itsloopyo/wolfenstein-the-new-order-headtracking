// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "window_centering.h"

#include <windows.h>

#include <cwchar>

#include "logging.h"
#include "window_placement.h"

namespace wolf_ht {

namespace {

constexpr int kPollIntervalMs = 250;
constexpr int kPollAttempts = 240;  // 60s, which covers a cold start off a hard disk.

// The rect has to hold still before it is worth acting on. The window is up
// before the engine has finished sizing and placing it, and nobody has measured
// when this game stops moving it, so the wait is on three seconds of an
// unchanged rect rather than on a fixed delay that would be a guess.
constexpr int kSettlePolls = 12;

// The render window's class, read out of the shipped EXE, where
// CreateWindowClasses registers it under the bare game name. The two other
// classes it registers are "wolf_WGL_FAKE" and "wolf_CONTEXT", and the engine's
// separate console window is "Wolfenstein The New Order WinConsole" - an exact
// class match is what keeps this off all three. Core's FindGameWindow takes the
// first visible unowned window of the process instead, which would centre a
// visible console and burn the one move on it.
constexpr wchar_t kRenderWindowClass[] = L"Wolfenstein The New Order";

struct FindState {
    DWORD pid = 0;
    HWND window = nullptr;
};

BOOL CALLBACK PickRenderWindow(HWND window, LPARAM lparam) {
    auto* state = reinterpret_cast<FindState*>(lparam);

    DWORD windowPid = 0;
    GetWindowThreadProcessId(window, &windowPid);
    if (windowPid != state->pid) return TRUE;
    if (!IsWindowVisible(window)) return TRUE;

    wchar_t className[64] = {};
    if (GetClassNameW(window, className, ARRAYSIZE(className)) == 0) return TRUE;
    if (std::wcscmp(className, kRenderWindowClass) != 0) return TRUE;

    state->window = window;
    return FALSE;
}

HWND FindRenderWindow() {
    FindState state;
    state.pid = GetCurrentProcessId();
    EnumWindows(PickRenderWindow, reinterpret_cast<LPARAM>(&state));
    return state.window;
}

void CenterUnlessAlready(HWND window, const RECT& rect) {
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("[window] GetMonitorInfoW failed: %lu; leaving placement alone", GetLastError());
        return;
    }

    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int left = static_cast<int>(rect.left);
    const int top = static_cast<int>(rect.top);

    POINT target = {};
    switch (DecidePlacement(rect, info.rcWork, info.rcMonitor, target)) {
        case Placement::AlreadyCentered:
            Log::Line("[window] %dx%d at (%d, %d) is already centred, leaving it alone", width,
                      height, left, top);
            return;
        case Placement::TooLargeForWorkArea:
            Log::Line("[window] %dx%d does not fit the %dx%d work area, leaving it alone", width,
                      height, static_cast<int>(info.rcWork.right - info.rcWork.left),
                      static_cast<int>(info.rcWork.bottom - info.rcWork.top));
            return;
        case Placement::Center:
            break;
    }

    if (!SetWindowPos(window, nullptr, target.x, target.y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("[window] SetWindowPos failed: %lu", GetLastError());
        return;
    }
    Log::Line("[window] centred the %dx%d window at (%d, %d), moved from (%d, %d)", width, height,
              static_cast<int>(target.x), static_cast<int>(target.y), left, top);
}

// Waits for a render window whose rect has held still for kSettlePolls, writing
// it and that rect to the out-params. False when none settled in time, in which
// case the out-params are left alone.
bool WaitForSettledWindow(HWND& settled, RECT& settledRect) {
    RECT previous = {};
    bool havePrevious = false;
    int stablePolls = 0;
    // Held across polls. Finding it asks every top-level window on the desktop
    // for its process and class name, and the wait is 240 polls long - a minute
    // of that four times a second to keep arriving at the same HWND. A poll
    // that cannot use the handle drops it and the next one looks again, so a
    // window that goes away is still found when it comes back.
    HWND window = nullptr;

    for (int attempt = 0; attempt < kPollAttempts; ++attempt) {
        Sleep(kPollIntervalMs);

        if (window == nullptr) window = FindRenderWindow();
        RECT current = {};
        // A minimised window reports a rect off the far top-left of the desktop,
        // and this game minimises itself whenever it loses focus - centring on
        // that rect would park the restored window somewhere arbitrary. Waiting
        // it out is the whole handling: the counter resets and the poll runs on.
        if (!window || !IsWindowVisible(window) || IsIconic(window) ||
            !GetWindowRect(window, &current)) {
            window = nullptr;
            havePrevious = false;
            stablePolls = 0;
            continue;
        }

        if (havePrevious && EqualRect(&previous, &current)) {
            if (++stablePolls < kSettlePolls) continue;
            settled = window;
            settledRect = current;
            return true;
        }
        previous = current;
        havePrevious = true;
        stablePolls = 0;
    }
    return false;
}

}  // namespace

void CenterWindowWhenReady() {
    HWND window = nullptr;
    RECT rect = {};
    if (!WaitForSettledWindow(window, rect)) {
        Log::Line("[window] no render window settled within %ds, leaving placement alone",
                  kPollAttempts * kPollIntervalMs / 1000);
        return;
    }
    CenterUnlessAlready(window, rect);
}

}  // namespace wolf_ht
