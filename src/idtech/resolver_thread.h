// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <utility>

namespace wolf_ht::idtech {

// The worker behind both engine-singleton resolvers. It runs `sweep` on its own
// thread whenever nothing is published, and publishes whatever that returns.
//
// A sweep reads a gigabyte and more of committed memory, which is nothing once
// per map load and a visible hitch if it happens on the render thread. So the
// render thread only ever reads the published pointer, and clearing that
// pointer is what asks for another sweep.
//
// Held here rather than written out per resolver: the two differ only in what
// they sweep for and what makes a candidate live, and a second copy of the stop
// flag, the publish and the teardown is a second place for them to be got
// subtly wrong.
class ResolverThread {
public:
    ~ResolverThread() {
        m_stop.store(true);
        // Detached, never joined. A sweep runs for seconds, and this destructor
        // runs while the game is tearing down.
        if (m_thread.joinable()) m_thread.detach();
    }

    void Start(std::function<void*()> sweep) {
        m_sweep = std::move(sweep);
        m_thread = std::thread(&ResolverThread::Worker, this);
    }

    void* Published() const { return m_object.load(); }
    void Clear() { m_object.store(nullptr); }

private:
    // How long the worker waits between sweeps while it has nothing to publish.
    static constexpr std::chrono::seconds kSweepInterval{3};

    void Worker() {
        while (!m_stop.load()) {
            if (m_object.load() == nullptr) {
                if (void* found = m_sweep()) m_object.store(found);
            }
            std::this_thread::sleep_for(kSweepInterval);
        }
    }

    std::function<void*()> m_sweep;
    std::atomic<void*> m_object{nullptr};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

}  // namespace wolf_ht::idtech
