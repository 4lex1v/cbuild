
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/meta.hpp"

#define defer auto tokenpaste(__deferred_lambda_call, __COUNTER__) = Fin::deferrer << [&] ()

namespace Fin {

template <typename Type>
struct Deferrable {
  Type cleanup;

  constexpr Deferrable (Type &&cb)
    : cleanup { move(cb) } {}

  constexpr ~Deferrable () { cleanup(); }
};

static struct {
  template <typename Type>
  constexpr Deferrable<Type> operator << (Type &&cb) {
    return Deferrable<Type>(forward<Type>(cb));
  }
} deferrer;

}
