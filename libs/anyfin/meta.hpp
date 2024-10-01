
#pragma once

#include "anyfin/base.hpp"

namespace Fin {

namespace internals {

template <typename T> struct Remove_Ref       { using type = T; };
template <typename T> struct Remove_Ref<T &>  { using type = T; };
template <typename T> struct Remove_Ref<T &&> { using type = T; };

template <typename T> struct Remove_Ptr      { using type = T; };
template <typename T> struct Remove_Ptr<T *> { using type = T; };

template <typename A, typename B> struct Copy_Value_Category               { using type = B; };
template <typename A, typename B> struct Copy_Value_Category<A *, B>       { using type = B *; };
template <typename A, typename B> struct Copy_Value_Category<A &, B>       { using type = B &; };
template <typename A, typename B> struct Copy_Value_Category<A &&, B>      { using type = B &&; };
template <typename A, typename B> struct Copy_Value_Category<const A, B>   { using type = const B; };
template <typename A, typename B> struct Copy_Value_Category<const A *, B> { using type = const B *; };
template <typename A, typename B> struct Copy_Value_Category<const A &, B> { using type = const B &; };

template <typename T> struct Raw_Type                     { using type = T; };
template <typename T> struct Raw_Type<const T>            { using type = T; };
template <typename T> struct Raw_Type<T &&>               { using type = T; };
template <typename T> struct Raw_Type<T &>                { using type = T; };
template <typename T> struct Raw_Type<T *>                { using type = T; };
template <typename T> struct Raw_Type<T * const>          { using type = T; };
template <typename T> struct Raw_Type<T * volatile>       { using type = T; };
template <typename T> struct Raw_Type<const T &>          { using type = T; };
template <typename T> struct Raw_Type<const T *>          { using type = T; };
template <typename T> struct Raw_Type<const T * const>    { using type = T; };
template <typename T> struct Raw_Type<const T * volatile> { using type = T; };
template <typename T, usize N> struct Raw_Type<T[N]>      { using type = T; };

}

template <typename A, typename B> constexpr inline bool same_types       = false;
template <typename A>             constexpr inline bool same_types<A, A> = true;

template <typename T> constexpr inline bool is_pointer = false;
template <typename T> constexpr inline bool is_pointer<T *> = true;
template <typename T> constexpr inline bool is_pointer<T * const> = true;
template <typename T> constexpr inline bool is_pointer<T * volatile> = true;
template <typename T> constexpr inline bool is_pointer<T * const volatile> = true;

template <typename T>          constexpr inline bool is_static_array             = false;
template <typename T, usize N> constexpr inline bool is_static_array<T[N]>       = true;
template <typename T, usize N> constexpr inline bool is_static_array<const T[N]> = true;

template <typename... T> using voided = void;

template <typename From, typename To>
constexpr inline bool is_convertible = __is_convertible_to(From, To);

template <typename T> constexpr T&& materialize () {}

template <typename T> using remove_ref = typename internals::Remove_Ref<T>::type;
template <typename T> using remove_ptr = typename internals::Remove_Ptr<T>::type;
template <typename T> using raw_type   = typename internals::Raw_Type<T>::type;

template <typename T> using ref_type = T&;

template <typename T>
fin_forceinline
constexpr remove_ref<T> && move (T &&value) {
  return static_cast<remove_ref<T> &&>(value);
}

template <typename T>
fin_forceinline
constexpr T&& forward (remove_ref<T>  &value) { return static_cast<T &&>(value); }

template <typename T>
fin_forceinline
constexpr T&& forward (remove_ref<T> &&value) { return static_cast<T &&>(value); }

template <typename A, typename B> using copy_value_category = internals::Copy_Value_Category<A, B>::type;

template <typename A, typename B>
concept Same_Types = same_types<A, B>;

template <typename A, typename B>
concept Convertible_To = is_convertible<A, B>;

template <typename F, typename Ret, typename... Args>
concept Invocable = requires (F func, Args... args) {
  { func(args...) } -> Same_Types<Ret>;
};

template <typename T>
concept Is_Reference = requires {
  typename ref_type<T>;
};

template <typename T> constexpr inline bool is_string_literal = same_types<raw_type<T>, char> && is_static_array<T>;

template <typename T>
concept Byte_Type = Same_Types<raw_type<T>, char> || Same_Types<raw_type<T>, u8>;

template <typename T, usize N>
concept Byte_Array = Byte_Type<raw_type<T>> && is_static_array<T[N]>;

template <typename T>
concept Byte_Pointer = Byte_Type<raw_type<T>> && is_pointer<T>;

template <typename I>
concept Signed_Integral = same_types<I, s8> || same_types<I, s16> || same_types<I, s32> || same_types<I, s64>;

template <typename I>
concept Unsigned_Integral = same_types<I, u8> || same_types<I, u16> || same_types<I, u32> || same_types<I, u64>;

template <typename I>
concept Integral = Signed_Integral<I> || Unsigned_Integral<I>;

template <typename T>
concept Numeric = Integral<T> || Same_Types<T, f32> || Same_Types<T, f64>;

template <typename I, typename T>
concept Iterator = requires (I iterator, const I &other) {
  { *iterator }         -> Convertible_To<T &>;
  { ++iterator }        -> Same_Types<I &>;
  { iterator != other } -> Same_Types<bool>;
};

template <typename C, typename T>
concept Container = requires (C container) {
  { container.begin() } -> Iterator<T>;
  { container.end() }   -> Iterator<T>;
};

template <typename C, typename T>
concept Iterable = Container<C, T> || is_static_array<C>;

namespace iterator {

template <typename T, usize N>
constexpr usize count (const T (&array)[N]) {
  return N;
}

}

}
