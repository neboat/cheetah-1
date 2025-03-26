#ifndef _WORKER_COORD_H
#define _WORKER_COORD_H

// Routines for coordinating workers, specifically, putting workers to sleep and
// waking workers when execution enters and leaves cilkified regions.

#include <atomic>
#include <stdint.h>
#include <limits.h>

#ifdef __linux__
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "global.h"

//=========================================================
// Common internal interface for managing execution of workers.
//=========================================================

__attribute__((always_inline)) static inline void busy_loop_pause() {
#ifdef __SSE__
    __builtin_ia32_pause();
#endif
#ifdef __aarch64__
    __builtin_arm_yield();
#endif
}

__attribute__((always_inline)) static inline void busy_pause(void) {
    for (int i = 0; i < BUSY_PAUSE; ++i)
        busy_loop_pause();
}

// Routines to update global flags to prevent workers from re-entering the
// work-stealing loop.  Note that we don't wait for the workers to exit the
// work-stealing loop, since its more efficient to allow that to happen
// eventually.

// Routines to control the cilkified state.

static inline void set_cilkified(global_state *g) {
    record_event(g, scheduler_event::CILKIFY, 0, NO_WORKER);
    // Set g->cilkified = true, indicating that the execution is now cilkified.
    g->cilkified.store(true, std::memory_order_release);
}

// Mark the computation as no longer cilkified and signal the thread that
// originally cilkified the execution.
static inline void signal_uncilkified(global_state *g) {
    record_event(g, scheduler_event::UNCILKIFY, 0, NO_WORKER);
    g->cilkified.store(false, std::memory_order_release);
    g->cilkified.notify_all();
}

// Wait on g->cilkified to be set to false, indicating the end of the Cilkified
// region.
static inline void wait_while_cilkified(global_state *g) {
    unsigned int fail = 0;
    while (fail++ < BUSY_LOOP_SPIN) {
        if (!g->cilkified.load(std::memory_order_acquire)) {
            return;
        }
        busy_pause();
    }
    record_event(g, scheduler_event::WAIT_CILKIFIED, 0, NO_WORKER);
    while (g->cilkified.load(std::memory_order_acquire)) {
        g->cilkified.wait(true);
    }
}

//=========================================================
// Operations to disengage and reengage workers within the work-stealing loop.
//=========================================================

// Reset the shared variable for disengaging thief threads.
static inline void reset_disengaged_var(global_state *g) {
    g->disengaged_thieves.store(0, std::memory_order_release);
}

// Request to reengage `count` thief threads.
static inline void request_more_thieves(global_state *g, worker_id self,
                                        uint32_t count) {
    CILK_ASSERT(count > 0);

    // Don't allow this routine increment the futex beyond half the number of
    // workers on the system.  This bounds how many successful steals can
    // possibly keep thieves engaged unnecessarily in the future, when there may
    // not be as much parallelism.
    int32_t max_requests = (int32_t)(g->nworkers / 2);

    // This step synchronizes with concurrent calls to request_more_thieves and
    // concurrent calls to try_to_disengage_thief.
    while (true) {
        uint32_t disengaged_thieves =
            g->disengaged_thieves.load(std::memory_order_acquire);

        int32_t max_to_wake = max_requests - disengaged_thieves;
        if (max_to_wake <= 0)
            return;
        uint64_t to_wake = max_to_wake < (int32_t)count ? max_to_wake : count;

        if (g->disengaged_thieves.compare_exchange_strong(
                disengaged_thieves, disengaged_thieves + to_wake,
                std::memory_order_release, std::memory_order_relaxed)) {
            record_event(g, scheduler_event::MORE_THIEVES, to_wake, self);
            // We successfully updated the futex.  Wake the thief threads
            // waiting on this futex.
            switch (to_wake) {
            case 3:
                g->disengaged_thieves.notify_one();
                [[fallthrough]];
            case 2:
                g->disengaged_thieves.notify_one();
                [[fallthrough]];
            case 1:
                g->disengaged_thieves.notify_one();
                break;
            default:
                g->disengaged_thieves.notify_all();
                break;
            }
            return;
        }
    }
}

static inline uint32_t thief_disengage(global_state *g, worker_id self) {

    // This step synchronizes with calls to request_more_thieves.
    while (true) {
        // Decrement the futex when woken up.  The loop and compare-exchange are
        // designed to handle cases where multiple threads waiting on the futex
        // were woken up and where there may be spurious wakeups.
        while (uint32_t val =
               g->disengaged_thieves.load(std::memory_order_relaxed)) {
            if (g->disengaged_thieves.compare_exchange_weak(val, val - 1,
                    std::memory_order_release, std::memory_order_relaxed)) {
                return val;
            }
            busy_loop_pause();
        }
        record_event(g, scheduler_event::WAIT_DISENGAGED, 0, self);
        g->disengaged_thieves.wait(0, std::memory_order_relaxed);
    }
}

// Signal to all disengaged thief threads to resume work-stealing.
static inline void wake_all_disengaged(global_state *g) {
    record_event(g, scheduler_event::ALL_THIEVES, 0, NO_WORKER);
    g->disengaged_thieves.store(INT_MAX, std::memory_order_release);
    g->disengaged_thieves.notify_all();
}

// Reset global state to make thief threads sleep for signal to start
// work-stealing again.
static inline void sleep_thieves(global_state *g) {
    reset_disengaged_var(g);
}

// Called by a thief thread.  Causes the thief thread to wait for a signal to
// start work-stealing.
static inline uint32_t thief_wait(global_state *g, worker_id self) {
    return thief_disengage(g, self);
}

// Called by a thief thread.  Check if the thief should start waiting for the
// start of a cilkified region.  If a new cilkified region has been started
// already, update the global state to indicate that this worker is engaged in
// work stealing.
static inline bool thief_should_wait(global_state *g) {
    while (uint32_t val =
           g->disengaged_thieves.load(std::memory_order_relaxed)) {
        if (g->disengaged_thieves.compare_exchange_weak(
                val, val - 1, std::memory_order_release,
                std::memory_order_relaxed))
            return false;
        busy_loop_pause();
    }
    return true;
}

// Signal the thief threads to start work-stealing (or terminate, if
// g->terminate == 1).
static inline void wake_thieves(global_state *g) {
    record_event(g, scheduler_event::ALL_THIEVES, 0, NO_WORKER);
    g->disengaged_thieves.store(g->nworkers - 1, std::memory_order_release);
    g->disengaged_thieves.notify_all();
}

#endif /* _WORKER_COORD_H */
