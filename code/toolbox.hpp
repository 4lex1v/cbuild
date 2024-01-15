
#pragma once

#include <intrin.h>

#include "anyfin/base.hpp"

#include "anyfin/core/option.hpp"
#include "anyfin/core/slice.hpp"

#include "cbuild.hpp"

static Option<usize> find_offset (const Slice<u64> &data, const u64 _value) {
  if (is_empty(data)) return opt_none;
  
  assert(is_aligned_by(data.value, 32));

  auto value = _mm256_set1_epi64x(_value);

  s32 step  = 4;
  s32 limit = data.count - step;

  s32 idx = 0;
  while (idx <= limit) {
    auto array  = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data.value + idx));
    auto result = _mm256_cmpeq_epi64(array, value);

    auto match = _mm256_movemask_epi8(result);
    if (match) return ((__builtin_ctz(match) >> 3) + idx);

    idx += step;
  }

  for (usize i = idx; i < data.count; i++)
    if (data[i] == _value) return i;

  return opt_none;
}

static bool contains_key (const Slice<u64> &data, u64 key) {
  return find_offset(data, key).is_some();
}

