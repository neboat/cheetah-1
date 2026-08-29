#include "cilk-internal.h"
#include "cilk/reducer"
#include "cilk2c_inlined.h"
// #include "local-hypertable.h"
#include "hyperobject_base.h"
#include "local-hyper-pagetable.h"
#include "local-reducer-api.h"
#include "rts-config.h"

using cilk::reducer_base;
using cilk::reducer_callbacks;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// reducer_base::reducer_base() {
//     // This would be a great place to register the reducer,
//     // but doing so would break the equivalence between
//     // leftmost view and dynamic views.  The derived class
//     // identity operation would need to pass a flag to this
//     // constructor to suppress registration.
// }

// reducer_base::~reducer_base() {}

__attribute__((always_inline))
static void reducer_register(uintptr_t key, cilk::reducer_data &&data) __CILKRTS_NOTHROW {
    struct hyper_table *table =
        get_local_hyper_table(__cilkrts_get_tls_worker());
    [[maybe_unused]] bool success =
        insert_hyperobject(table, key, std::forward<reducer_data>(data));
    CILK_ASSERT(success && "Failed to register reducer.");
}

// void __cilkrts_reducer_register_0(reducer_base *key) __CILKRTS_NOTHROW {
//     reducer_register((uintptr_t)key, {.view = nullptr, .extra = key});
// }

void __cilkrts_reducer_register_0(reducer_base *key, cilk::rb_reduce_fn reduce) __CILKRTS_NOTHROW {
    reducer_register((uintptr_t)key, {.view = key, .extra = reduce});
}

void __cilkrts_reducer_register_1(void *key,
                                  reducer_callbacks *cb) __CILKRTS_NOTHROW {
    reducer_register((uintptr_t)key, {.view = key, .extra = &cb->reduce});
}

void __cilkrts_reducer_register_2(void *key, __cilk_c_reduce_fn *reduce)
    __CILKRTS_NOTHROW {
    reducer_register((uintptr_t)key, {.view = key, .extra = reduce});
}

void __cilkrts_reducer_unregister(void *key) noexcept {
    if (struct hyper_table *table = get_hyper_table()) {
        [[maybe_unused]] bool success =
            remove_hyperobject(table, (uintptr_t)key);
        // CILK_ASSERT(success && "Failed to unregister reducer.");
    }
}

#pragma clang diagnostic pop

CHEETAH_INTERNAL
reducer_base *internal_reducer_lookup(__cilkrts_worker *w, reducer_base *key,
                                      cilk::view_size_fn size_fn,
                                      cilk::rb_identity_fn ident_fn,
                                      cilk::rb_reduce_fn red_fn) {
    struct hyper_table *table = get_local_hyper_table(w);
    bucket *b = find_hyperobject(table, (uintptr_t)key);
    if (__builtin_expect(!!b, true)) {
        CILK_ASSERT_POINTER_EQUAL(key, (void *)getAddrFromKey(b->key));
        // Return the existing view.
        // return std::get<reducer_base *>(b->data.extra);
        return static_cast<reducer_base *>(b->data.view);
    }

    return __cilkrts_insert_new_view_0(table, key, size_fn, ident_fn, red_fn);
}

// CHEETAH_INTERNAL
// reducer_base *internal_reducer_lookup(__cilkrts_worker *w, reducer_base *key,
//                                       size_t size,
//                                       cilk::rb_identity_fn ident_fn,
//                                       cilk::rb_reduce_fn red_fn) {
//     struct hyper_table *table = get_local_hyper_table(w);
//     bucket *b = find_hyperobject(table, (uintptr_t)key);
//     if (__builtin_expect(!!b, true)) {
//         CILK_ASSERT_POINTER_EQUAL(key, (void *)getAddrFromKey(b->key));
//         // Return the existing view.
//         // return std::get<reducer_base *>(b->data.extra);
//         return static_cast<reducer_base *>(b->data.view);
//     }

//     return __cilkrts_insert_new_view_0(table, key, size, ident_fn, red_fn);
// }

CHEETAH_INTERNAL
void internal_reducer_remove(__cilkrts_worker *w, void *key) {
    struct hyper_table *table = get_local_hyper_table(w);
    [[maybe_unused]] bool success = remove_hyperobject(table, (uintptr_t)key);
}
