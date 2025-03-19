#ifndef _CLOSURE_H
#define _CLOSURE_H

#include <atomic>
#include <new>
#include "debug.h"

#include "cilk-internal.h"
#include "fiber.h"
#include "mutex.h"

#include "closure-type.h"

#if CILK_DEBUG
static inline void Closure_assert_ownership(worker_id self, Closure *t) {
    CILK_ASSERT(t->mutex_owner.load(std::memory_order_relaxed) == self);
}

static inline void Closure_assert_alienation(worker_id self, Closure *t) {
    CILK_ASSERT(t->mutex_owner.load(std::memory_order_relaxed) != self);
}

static inline void Closure_checkmagic(Closure *t) {
    switch (t->status) {
    case CLOSURE_RUNNING:
    case CLOSURE_SUSPENDED:
    case CLOSURE_RETURNING:
    case CLOSURE_READY:
        return;
    case CLOSURE_POST_INVALID:
        CILK_ABORT("destroyed closure");
    default:
        CILK_ABORT("invalid closure");
    }
}

#define Closure_assert_ownership(s, t) Closure_assert_ownership(s, t)
#define Closure_assert_alienation(s, t) Closure_assert_alienation(s, t)
#define Closure_checkmagic(t) Closure_checkmagic(t)
#else
#define Closure_assert_ownership(s, t)
#define Closure_assert_alienation(s, t)
#define Closure_checkmagic(t)
#endif // CILK_DEBUG

#include "cilk2c.h"
#include "global.h"
#include "internal-malloc.h"
#include "readydeque.h"

static inline void Closure_lock(worker_id self, Closure *t) {
    Closure_checkmagic(t);
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

static inline void Closure_unlock(worker_id self, Closure *t) {
    (void)self; // unused if assertions disabled
    Closure_checkmagic(t);
    Closure_assert_ownership(self, t);
    t->mutex_owner.store(NO_WORKER, std::memory_order_release);
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

static inline Closure *Closure_create(__cilkrts_worker *const w,
                                      __cilkrts_stack_frame *sf) {
    /* cilk_internal_malloc returns sufficiently aligned memory */
    void *new_closure =
        cilk_internal_malloc(w, sizeof(Closure), IM_CLOSURE);
    CILK_ASSERT(new_closure != nullptr);

    cilkrts_alert(CLOSURE, "Allocate closure %p", (void *)new_closure);

    return new(new_closure) Closure(sf);
}

// double linking left and right; the right is always the new child
// Note that we must have the lock on the parent when invoking this function
static inline void double_link_children(Closure *left, Closure *right) {

    if (left) {
        CILK_ASSERT_NULL(left->right_sib);
        left->right_sib = right;
    }

    if (right) {
        CILK_ASSERT_NULL(right->left_sib);
        right->left_sib = left;
    }
}

// unlink the closure from its left and right siblings
// Note that we must have the lock on the parent when invoking this function
static inline void unlink_child(Closure *cl) {

    if (cl->left_sib) {
        CILK_ASSERT_POINTER_EQUAL(cl->left_sib->right_sib, cl);
        cl->left_sib->right_sib = cl->right_sib;
    }
    if (cl->right_sib) {
        CILK_ASSERT_POINTER_EQUAL(cl->right_sib->left_sib, cl);
        cl->right_sib->left_sib = cl->left_sib;
    }
    // used only for error checking
    cl->left_sib = nullptr;
    cl->right_sib = nullptr;
}

/* ANGE: destroy the closure and internally free it (put back to global
   pool) */
static inline void Closure_destroy(struct __cilkrts_worker *const w,
                                   Closure *t) {
    cilkrts_alert(CLOSURE, "Deallocate closure %p", (void *)t);
    t->~Closure();
    cilk_internal_free(w, t, sizeof(*t), IM_CLOSURE);
}

/* Destroy the closure and internally free it (put back to global pool), after
   workers have been terminated. */
static inline void Closure_destroy_global(struct global_state *const g,
                                          Closure *t) {
    cilkrts_alert(CLOSURE, "Deallocate closure %p", (void *)t);
    t->~Closure();
    cilk_internal_free_global(g, t, sizeof(*t), IM_CLOSURE);
}

#endif
