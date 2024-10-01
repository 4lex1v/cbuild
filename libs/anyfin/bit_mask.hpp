
#pragma once

#include "anyfin/base.hpp"

namespace Fin {

template <typename E, typename Mask = u64>
struct Bit_Mask {
  Mask bit_mask;

  fin_forceinline constexpr Bit_Mask (): bit_mask { static_cast<Mask>(0) } {}
  fin_forceinline constexpr Bit_Mask (Mask value): bit_mask { value } {}
  fin_forceinline constexpr Bit_Mask (E value): bit_mask { static_cast<Mask>(value) } {}
  
  fin_forceinline constexpr Bit_Mask<E> operator | (this auto self, E value) { return self.bit_mask | static_cast<Mask>(value); }
  fin_forceinline constexpr bool        operator & (this auto self, E value) { return self.bit_mask & static_cast<Mask>(value); }
 
  fin_forceinline constexpr Bit_Mask<E> set    (this auto self, E value) { return self |= value; }
  fin_forceinline constexpr bool        is_set (this auto self, E value) { return self & value; }
};

template <typename E>
fin_forceinline
constexpr Bit_Mask<E> operator | (E left, E right) { return Bit_Mask(left) | right; }

}
