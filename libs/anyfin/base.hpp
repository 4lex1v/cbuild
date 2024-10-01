
#pragma once

using s8  = signed char;
using s16 = short;
using s32 = int;
using s64 = long long;

using u8  = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;

using b8  = u8;
using b16 = u16;
using b32 = u32;
using b64 = u64;

using f32 = float;
using f64 = double;

using usize = decltype(sizeof(void *));

#define tokenpaste2(X, Y) X##Y
#define tokenpaste(X, Y) tokenpaste2(X, Y)

#define stringify2(X) #X
#define stringify(X) stringify2(X)

#define fin_flag(N) 1 << (N)

#define fin_forceinline __attribute__((always_inline))

#ifdef DEV_BUILD
  #define fin_ensure(EXPR) do { if (!static_cast<bool>(EXPR)) __builtin_debugtrap(); } while (0)
#else
  #define fin_ensure(EXPR)
#endif
