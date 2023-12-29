
#pragma once

#include <concepts>
#include <type_traits>
#include <source_location>

#include "base.hpp"

#define loctag() (__FILE__ "(" stringify(__LINE__) "): " stringify(__FUNCTION__))

[[noreturn]] void raise_error_and_halt (const char *location_tag, const char *message);

[[noreturn]] void trap (const char *message, const std::source_location loc = std::source_location::current());

#ifdef DEV_BUILD
#define assert(EXPR)                                                    \
  do {                                                                  \
    if (!static_cast<bool>(EXPR)) raise_error_and_halt(loctag(), stringify(EXPR)); \
  } while (false)

#define fassert(EXPR, FMT, ...)                                         \
  do {                                                                  \
    enum { buffer_size = 1024 };                                        \
    char buffer [buffer_size] {};                                       \
    Memory_Arena local { buffer, buffer_size };                         \
    auto message = format_string(&local, "Expr: %\n" FMT, stringify(EXPR), __VA_ARGS__); \
    if (!static_cast<bool>(EXPR)) raise_error_and_halt(loctag(), message); \
  } while (false)

#else
#define assert(EXPR)
#define fassert(EXPR, FMT, ...)
#endif

#define todo() assert(false && "Unimplemented");

//#define trap(ARENA, FMT, ...) raise_error_and_halt(loctag(), format_string(ARENA, FMT, __VA_ARGS__))

template <typename N> constexpr N max (N a, N b) { return (a > b ? a : b); }
template <typename N> constexpr N min (N a, N b) { return (a > b ? b : a); }

template <typename T> struct Capture_Type { using type = T; };
template <typename T> using Value_Type = Capture_Type<T>::type;

template <typename T>
constexpr T clamp (T value, Value_Type<T> minValue, Value_Type<T> maxValue) {
  if      (value < minValue) return minValue;
  else if (value > maxValue) return maxValue;

  return value;
}

template <typename N> consteval N kilobytes (const N value) { return value * 1024; }
template <typename N> consteval N megabytes (const N value) { return kilobytes(value) * 1024; }

template <typename T> constexpr bool is_power_of_2 (const T value) { return (value > 0) && ((value & (value - 1)) == 0); }

template <typename T> struct is_pointer     { static constexpr bool value = false; };
template <typename T> struct is_pointer<T*> { static constexpr bool value = true;  };

template <typename T> constexpr inline bool is_pointer_v = is_pointer<T>::value;

template <typename A, typename B> struct same_types_s       { static constexpr bool value = false; };
template <typename T>             struct same_types_s<T, T> { static constexpr bool value = true;  };

template <typename T, usize N>
constexpr usize array_count_elements (const T (&)[N]) { return N; }

template <typename T>
constexpr T align_forward (const T value, const usize by) {  
  assert(is_power_of_2(by));

  if constexpr (!is_pointer_v<T>) return (value + (by - 1)) & ~(by - 1);  
  else return reinterpret_cast<T>((reinterpret_cast<usize>(value) + (by - 1)) & ~(by - 1));
}

template <bool B, typename T = void> struct check_condition          { };
template <typename T>                struct check_condition<true, T> { using type = T; };

template <typename T> struct remove_ref      { using type = T; };
template <typename T> struct remove_ref<T&>  { using type = T; };
template <typename T> struct remove_ref<T&&> { using type = T; };

template <typename T> struct remove_ptr      { using type = T; };
template <typename T> struct remove_ptr<T *> { using type = T; };
template <typename T> using remove_ptr_t = typename remove_ptr<T>::type;

template <typename T> cb_forceinline constexpr T&& forward (typename remove_ref<T>::type &value)  { return static_cast<T&&>(value); }
template <typename T> cb_forceinline constexpr T&& forward (typename remove_ref<T>::type &&value) { return static_cast<T&&>(value); }

template <typename T> cb_forceinline constexpr T && move (T &ref) { return static_cast<T &&>(ref); }

template <typename Type>
class Deferrable {
  Type cleanup;

public:
  explicit Deferrable (Type &&cb): cleanup{ forward<Type>(cb) } {}
  ~Deferrable () { cleanup(); }
};

static struct {
  template <typename Type>
  Deferrable<Type> operator << (Type &&cb) {
    return Deferrable<Type>(forward<Type>(cb));
  }
} deferrer;

#define defer auto tokenpaste(__deferred_lambda_call, __COUNTER__) = deferrer << [&]

template <typename E>
struct Bit_Mask {
  using Mask = u64;
  
  Mask bit_mask;

  constexpr Bit_Mask (): bit_mask { 0 } {}
  constexpr Bit_Mask (Mask value): bit_mask { value } {}
  constexpr Bit_Mask (E value): bit_mask { static_cast<Mask>(value) } {}

  constexpr Bit_Mask<E> operator | (E value) { return bit_mask | static_cast<Mask>(value); }

  constexpr bool operator & (E value) const { return bit_mask & static_cast<Mask>(value); }

  constexpr void set    (E value)       { this->bit_mask |= static_cast<Mask>(value); }
  constexpr bool is_set (E value) const { return this->bit_mask & static_cast<Mask>(value); }
};

template <typename E>
cb_forceinline constexpr Bit_Mask<E> operator | (E left, E right) { return Bit_Mask(left) | right; }

struct Memory_Region {
  char  *memory;
  usize  size;
};


