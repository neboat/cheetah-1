#ifndef _CILK_LOCAL_H
#define _CILK_LOCAL_H

#include <stdbool.h>

#include "internal-malloc-impl.h" /* for cilk_im_desc */
#include "fiber.h"
#include "jmpbuf.h"
#include "local-hypertable.h"
#include "sched_stats.h"
#include "types.h"

struct local_state {
    struct __cilkrts_stack_frame **shadow_stack;

    unsigned short state; /* __cilkrts_worker_state */
    bool provably_good_steal;
    bool exiting;
    bool returning;
    unsigned int rand_next;
    uint32_t wake_val;

    jmpbuf rts_ctx;
    hyper_table *lht;
    hyper_table *rht;
    struct cilk_fiber_pool fiber_pool;
    struct cilk_im_desc im_desc;
    struct sched_stats stats;
};

#endif /* _CILK_LOCAL_H */
