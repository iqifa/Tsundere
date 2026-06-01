#pragma once

#include <thread>
#include <cassert>

// Record the main thread ID at startup, assert current thread is main thread.
// Usage: add T_ASSERT_MAIN_THREAD() at the top of functions that must run
// on the main (GL/ImGui) thread.

inline std::thread::id g_MainThreadID = std::this_thread::get_id();

#define T_ASSERT_MAIN_THREAD() \
    assert(std::this_thread::get_id() == g_MainThreadID && \
           "This function must be called from the main thread!")
