
#pragma once

#include <cstdint>

using s8  = int8_t;
using s32 = int32_t;
using s64 = int64_t;

using u8 = uint8_t;
using b1 = u8;

using u16 = uint16_t;
using b2  = u16;

using u32 = uint32_t;
using b4  = u32;

using u64 = uint64_t;
using b8  = u64;

using usize = size_t;
using psize = usize;

#define tokenpaste2(X, Y) X##Y
#define tokenpaste(X, Y) tokenpaste2(X, Y)

#define stringify2(X) #X
#define stringify(X) stringify2(X)

#define flag(N) 1 << (N)

#define use(NAME) using enum NAME::Value

#define cb_forceinline __attribute__((always_inline))
#define alloc_array(TYPE, COUNT) reinterpret_cast<TYPE *>(_alloca((COUNT) * sizeof(TYPE)))

template <typename A, typename B>
struct Pair {
  A first;
  B second;
};

