
#pragma once

#include "anyfin/base.hpp"

namespace Fin {

// Parameter table: https://en.wikipedia.org/wiki/Linear_congruential_generator
struct Linear_Conguential_Generator {
  static const u64 a = 1664525;
  static const u64 c = 1013904223;
  static const u64 m = 4294967296;
  
  u64 state;

  Linear_Conguential_Generator (u64 seed): state { seed } {}
};

static inline u64 get_random (Linear_Conguential_Generator &lcg) {
  lcg.state = (lcg.a * lcg.state + lcg.c) % lcg.m;
  return lcg.state;
}

static inline u64 get_random_in_range (Linear_Conguential_Generator &lcg, u64 min, u64 max) {
  const auto value      = get_random(lcg);
  const auto normalized = static_cast<f64>(value) / (static_cast<f64>(lcg.m) - 1);
  return static_cast<int>(min + normalized * (max - min + 1));
}

} // namespace Fin
