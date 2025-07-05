#ifndef _CPP_REDUCER_H
#define _CPP_REDUCER_H

#include <cilk/reducer>

#ifdef __cplusplus

#include <functional>
#include <limits>

namespace cilk {

template <typename C, typename T> class reducer {
public:
    static void identity(void *v) = delete;
    static void reduce(void *l, void *r) = delete;
};

template <typename T> class reducer<std::plus<>, T> {
public:
    static void identity(void *v) { new (v) T{0}; }
    static void reduce(void *l, void *r) {
        *static_cast<T *>(l) =
            std::plus<>{}(*static_cast<T *>(l), *static_cast<T *>(r));
    }
};

template <typename T> class reducer<std::multiplies<>, T> {
public:
    static void identity(void *v) { new (v) T{1}; }
    static void reduce(void *l, void *r) {
        *static_cast<T *>(l) =
            std::multiplies<>{}(*static_cast<T *>(l), *static_cast<T *>(r));
    }
};

template <typename T> class reducer<std::bit_xor<>, T> {
public:
    static void identity(void *v) { new (v) T{0}; }
    static void reduce(void *l, void *r) {
        *static_cast<T *>(l) =
            std::bit_xor<>{}(*static_cast<T *>(l), *static_cast<T *>(r));
    }
};

template <typename T> class max_reducer {
public:
    static void identity(void *v) { new (v) T{std::numeric_limits<T>::lowest()}; }
    static void reduce(void *l, void *r) {
        *static_cast<T *>(l) =
            std::max(*static_cast<T *>(l), *static_cast<T *>(r));
    }
};

} // namespace cilk

#endif // #ifdef __cplusplus

#endif // _CPP_REDUCER_H