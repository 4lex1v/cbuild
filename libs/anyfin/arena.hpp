
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/memory.hpp"
#include "anyfin/meta.hpp"
#include "anyfin/prelude.hpp"

namespace Fin {

struct Memory_Arena {
  u8 *memory   = nullptr;
  usize size   = 0;
  usize offset = 0;

  constexpr Memory_Arena (u8 *_memory, const usize _size)
    : memory { _memory },
      size   { _size },
      offset { 0 }
  {
    fin_ensure(_memory);
    fin_ensure(size > sizeof(void*));
  }

  template <usize N>
  constexpr Memory_Arena (Byte_Array<N> auto (&array)[N])
    : Memory_Arena(cast_bytes<u8>(array), N) {}

  constexpr Memory_Arena (Memory_Region &&other)
    : Memory_Arena(other.memory, other.size)
  {
    other.memory = nullptr;
    other.size   = 0;
  }
};

template <typename T = u8>
static T * reserve (Memory_Arena &arena, usize size = sizeof(T), usize alignment = alignof(T)) {
  fin_ensure(size > 0);
  fin_ensure(alignment > 0);
  
  auto base         = arena.memory + arena.offset;
  auto aligned_base = align_forward(base, alignment);

  auto alignment_shift  = static_cast<usize>(aligned_base - base);
  auto reservation_size = alignment_shift + size;
  
  fin_ensure((reservation_size + arena.offset) < arena.size);
  if ((reservation_size + arena.offset) > arena.size) [[unlikely]] return nullptr;

  arena.offset += reservation_size;

  return reinterpret_cast<T *>(aligned_base);
}

static void reset_arena (Memory_Arena &arena) {
  arena.offset = 0;
}

static usize get_remaining_size (const Memory_Arena &arena) {
  return arena.size - arena.offset;
}

template <typename T = u8>
static T * get_memory_at_current_offset (Memory_Arena &arena, const usize alignment = alignof(T)) {
  return reinterpret_cast<T *>(align_forward(arena.memory + arena.offset, alignment));
}

static Memory_Arena make_sub_arena (Memory_Arena &arena, const usize size, const usize alignment = alignof(void*)) {
  auto reservation = reserve<u8>(arena, size, alignment);
  fin_ensure(reservation != nullptr);

  return Memory_Arena(reservation, size);
}

}
