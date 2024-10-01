
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/meta.hpp"

namespace Fin {

struct None {};
constexpr inline None opt_none {};

template <typename T>
struct Option {
  using Value_Type = T;

  bool has_value = false;
  T    value;

  fin_forceinline constexpr Option () = default;
  fin_forceinline constexpr Option (None): has_value { false } {}

  fin_forceinline
  constexpr Option (Value_Type &&_value)
    : has_value { true }, value { _value } {}

  fin_forceinline
  constexpr Option (Option<T> &&other)
    : has_value { other.has_value },
      value     { move(other.value) }
  {
    other.has_value = false;
  }

  fin_forceinline
  constexpr Option (const Option<T> &other)
    : has_value { true },
      value     { other.value }
  {}

  fin_forceinline constexpr bool is_some () const { return this->has_value; }
  fin_forceinline constexpr bool is_none () const { return !this->has_value; }

  fin_forceinline constexpr operator bool () const { return is_some(); }

  fin_forceinline
  constexpr Option<T>& operator = (Option<T> &&other) {
    this->has_value = true;
    this->value     = move(other.value);

    return *this;
  }

  fin_forceinline
  constexpr const remove_ref<Value_Type> * operator -> (this const auto &self) {
    if (self.is_none()) return nullptr;
    return &self.value;
  }

  fin_forceinline
  constexpr Value_Type && or_default (Value_Type default_value = {}) {
    return move(is_some() ? this->value : default_value);
  }

};

}

