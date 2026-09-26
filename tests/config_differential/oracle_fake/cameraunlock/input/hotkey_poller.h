#pragma once

// Stands in for core's HotkeyPoller in the hotkey oracle library only, so the published
// build's Hotkeys::Start (oracle/src/hotkeys.cpp, verbatim) runs with no polling thread and the
// test sees what it registered. Compiled with that library's renaming, so it cannot collide with
// the real poller the current core links. The real poller runs the toggle key's callback and
// every hotkey's callback on the same key-down edge, and skips code 0.

#include <functional>
#include <vector>

namespace cameraunlock::input {

using HotkeyCallback = std::function<void()>;

struct FakeRegistration {
    int vk;
    HotkeyCallback callback;
};

// Every registration any poller made since the test last cleared it, in order.
std::vector<FakeRegistration>& FakeRegistrations();

class HotkeyPoller {
public:
    void SetToggleKey(int vkCode, HotkeyCallback callback) {
        FakeRegistrations().push_back({vkCode, std::move(callback)});
    }
    int AddHotkey(int vkCode, HotkeyCallback callback) {
        FakeRegistrations().push_back({vkCode, std::move(callback)});
        return static_cast<int>(FakeRegistrations().size());
    }
    void Start(int) {}
};

}  // namespace cameraunlock::input
