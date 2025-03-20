#ifndef _CLOSURE_H
#define _CLOSURE_H

#include <atomic>
#include <new>
#include "debug.h"

#include "cilk-internal.h"
#include "fiber.h"
#include "mutex.h"

#include "closure-type.h"

#include "cilk2c.h"
#include "global.h"
#include "internal-malloc.h"
#include "readydeque.h"

static inline void Closure_lock(worker_id self, Closure *t) {
    t->checkmagic();
    while (true) {
        worker_id current_owner =
            t->mutex_owner.load(std::memory_order_relaxed);
        if ((current_owner == NO_WORKER) &&
            t->mutex_owner.compare_exchange_weak(
                current_owner, self, std::memory_order_acq_rel,
                std::memory_order_relaxed))
            break;
        busy_loop_pause();
    }
}

// need to be careful when calling this function --- we check whether a
// frame is set stolen (i.e., has a full frame associated with it), but note
// that the setting of this can be delayed.  A thief can steal a spawned
// frame, but it cannot fully promote it until it remaps its TLMM stack,
// because the flag field is stored in the frame on the TLMM stack.  That
// means, a frame can be stolen, in the process of being promoted, and
// mean while, the stolen flag is not set until finish_promote.
static inline int Closure_at_top_of_stack(__cilkrts_worker *const w,
                                          __cilkrts_stack_frame *const frame) {
    __cilkrts_stack_frame **head = w->head.load(std::memory_order_relaxed);
    __cilkrts_stack_frame **tail = w->tail.load(std::memory_order_relaxed);
    return (head == tail && __cilkrts_stolen(frame));
}

#endif
