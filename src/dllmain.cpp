// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "pch.h"

#include <exception>

#include "cameraunlock/diagnostics/crash_handler.h"
#include "headtracking_mod.h"
#include "logging.h"
#include "window_centering.h"

namespace {

// The mod's outermost frame. An exception leaving a thread entry point is
// std::terminate, which kills the game the mod is a guest in - so the whole of
// setup sits inside the guard, and a failure leaves a dormant mod and a log line
// rather than a dead process.
DWORD WINAPI BootstrapThread(LPVOID) {
    try {
        wolf_ht::OpenLogFile();
        wolf_ht::Log::Line("[main] WolfensteinTheNewOrderHeadTracking %s loaded into pid %lu",
                           HEADTRACKING_VERSION_STRING, GetCurrentProcessId());
        // The catch below only covers this thread. The render-view hook writes
        // through engine pointers on the game's own thread every frame, and a
        // fault there is otherwise a crash with nothing in HeadTracking.log to
        // say whether this mod was in the stack - which is the whole content of
        // a "the game crashes with the mod installed" report. Core's filter
        // chains to whatever the game installed, so its crash flow is unchanged.
        cameraunlock::diagnostics::InstallCrashHandler();
        wolf_ht::GetMod().Initialize();
        // Last, because it blocks for as long as the game takes to bring its
        // window up and hold it still. Nothing else runs on this thread once
        // Initialize has handed the work to the hook, the receiver and the
        // hotkey poller.
        //
        // Skipped entirely on a build the mod does not recognise. Moving a
        // player's window is a change they can see, and the build-profile
        // doctrine is that a mismatch leaves the game running exactly as it
        // would without the mod.
        if (wolf_ht::GetMod().IsEngaged()) wolf_ht::CenterWindowWhenReady();
    } catch (const std::exception& e) {
        wolf_ht::Log::Line("[main] startup failed (%s) - the mod is dormant, the game is "
                           "unaffected", e.what());
    } catch (...) {
        wolf_ht::Log::Line("[main] startup failed - the mod is dormant, the game is unaffected");
    }
    return 0;
}

// Takes a permanent reference on this module, so an unload cannot pull the code
// out from under the bootstrap thread or the installed hook.
void PinSelf() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCWSTR>(&BootstrapThread), &self);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            // Pinned before the thread that outlives DllMain is started: a
            // FreeLibrary while the bootstrap thread is still inside config
            // loading leaves that thread running in freed memory, which is a
            // crash in a module the debugger can only name "_unloaded".
            PinSelf();
            // The handle is closed straight away and the thread runs on. Nothing
            // ever joins it - see the detach case - so holding it would only keep
            // a kernel object alive for the life of the process.
            if (HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr)) {
                CloseHandle(thread);
            } else {
                // The log file is opened by that thread, so a failure here
                // leaves no HeadTracking.log and nothing anywhere to explain an
                // ASI that loaded and did nothing. OutputDebugStringA is the one
                // diagnostic that is safe under the loader lock.
                OutputDebugStringA("[WolfensteinTheNewOrderHeadTracking] could not start the "
                                   "bootstrap thread; the mod is dormant\n");
            }
            break;

        case DLL_PROCESS_DETACH:
            // Nothing. Not on process exit: the OS has already killed our worker
            // threads, possibly mid-syscall or holding the log mutex, so joining
            // or unhooking then can hang the game on the way out. And not on a
            // FreeLibrary either: DllMain runs under the loader lock, and joining
            // the hotkey and receiver threads needs that same lock for their own
            // exit path, which is a textbook deadlock. An ASI plugin is never
            // unloaded in practice; process teardown reclaims everything.
            break;
    }
    return TRUE;
}
