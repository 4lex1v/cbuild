
#pragma once

#include <immintrin.h>

#include "base.hpp"
#include "core.hpp"

static bool find_offset_intrinsic (usize *_result, const u64 *_array, usize count, u64 _value) {
  if (count == 0) return false;
  
  assert(is_aligned_by(_array, 32));

  auto value = _mm256_set1_epi64x(_value);

  s32 step  = 4;
  s32 limit = count - step;

  s32 idx = 0;
  while (idx <= limit) {
    auto array  = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(_array + idx));
    auto result = _mm256_cmpeq_epi64(array, value);

    auto match = _mm256_movemask_epi8(result);
    if (match) {
      *_result = ((__builtin_ctz(match) >> 3) + idx);
      return true;
    }

    idx += step;
  }

  for (usize i = idx; i < count; i++) {
    if (_array[i] == _value) {
      *_result = i;
      return true;
    } 
  }

  return false;
}

static cb_forceinline bool contains_key (const u64 *array, usize count, u64 key) {
  usize ignored = 0;
  return find_offset_intrinsic(&ignored, array, count, key);
}

static cb_forceinline bool find_offset (usize *result, const u64 *array, usize count, u64 value) {
  return find_offset_intrinsic(result, array, count, value);
}


