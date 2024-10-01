
#pragma once

#include "anyfin/meta.hpp"
#include "anyfin/option.hpp"

namespace Fin {

template <typename T>
struct Error {
  using Value_Type = T;

  Value_Type value;

  constexpr Error (const Value_Type &_value): value { _value } {}
  constexpr Error (Value_Type &&_value): value { move(_value) } {}

  template <typename... Args>
  constexpr Error (Args&&... args)
    : value { Value_Type(forward<Args>(args)...) }
  {}
};

template <typename T>
struct Ok {
  using Value_Type = T;

  Value_Type value;

  constexpr Ok (const Value_Type &_value): value { _value } {}
  constexpr Ok (Value_Type &&_value): value { move(_value) } {}

  template <typename... Args>
  constexpr Ok (Args&&... args)
    : value { Value_Type(forward<Args>(args)...) }
  {}
};

template <>
struct Ok<void> {
  using Value_Type = void;
  constexpr Ok () {};
};

/*
  Deduction guide for the void specialization.
 */
Ok() -> Ok<void>;

template <typename E, typename T>
struct Result {
  using Error_Type = remove_ref<E>;
  using Value_Type = remove_ref<T>;

  Option<Error_Type> error;
  Value_Type value;

  constexpr Result (Error<E>&& error):    error { move(error.value) } {}
  constexpr Result (Error_Type &&status): error { move(status)      } {}

  constexpr Result (Ok<T>&& ok):         value { move(ok.value) } {}
  constexpr Result (Value_Type &&value): value { move(value)    } {}

  constexpr bool is_ok    () const { return error.is_none(); }
  constexpr bool is_error () const { return error.is_some(); }

  constexpr T && or_default (this Result<E, T> &&self, T _default = {}) {
    return move(self.is_ok() ? self.value : _default);
  }

  constexpr void handle_value (const Invocable<void, const Value_Type &> auto &func) const {
    if (this->is_ok()) [[likely]] func(this->value);
  }
};

template <typename E>
struct Result<E, void> {
  using Error_Type = remove_ref<E>;

  Option<Error_Type> error;

  constexpr Result (Error<E>&& error): error { move(error.value) } {}
  constexpr Result (Error_Type &&status): error { move(status) } {}

  constexpr Result (Ok<void>&&): error {} {}

  constexpr bool is_ok    (this const auto &self) { return self.error.is_none(); }
  constexpr bool is_error (this const auto &self) { return self.error.is_some(); }
};

/*
  
 */
#define fin_check(RESULT)                               \
  do {                                                  \
    if (auto result = (RESULT); result.is_error())      \
      return ::Fin::Error(::Fin::move(result.error.value));  \
  } while (0)

}

