
#pragma once

#include "base.hpp"
#include "core.hpp"

template <typename T> struct Seq;

struct Memory_Arena {
  char  *memory;
  usize  size;
  usize  offset = 0;

  Memory_Arena (char *_memory, usize _size): memory { _memory }, size   { _size } {}
  Memory_Arena (Memory_Region region): memory { region.memory }, size   { region.size } {}
};

constexpr void reset_arena (Memory_Arena *arena) {
  arena->offset = 0;
}

template <typename T = char>
constexpr T * get_memory_at_current_offset (Memory_Arena *arena, usize alignment = alignof(T)) {
  return reinterpret_cast<T *>(align_forward(arena->memory + arena->offset, alignment));
}

constexpr bool has_space (Memory_Arena *arena, usize size, usize alignment) {
  auto base         = arena->memory + arena->offset;
  auto aligned_base = align_forward(base, alignment);

  auto aligned_diff     = static_cast<usize>(aligned_base - base);
  auto reservation_size = aligned_diff + size;

  return ((reservation_size + arena->offset) <= arena->size);
}

constexpr usize get_remaining_size (Memory_Arena *arena) {
  return arena->size - arena->offset;
}

constexpr char * reserve_memory (Memory_Arena *arena, usize size, usize alignment = alignof(void *)) {
  auto base         = arena->memory + arena->offset;
  auto aligned_base = align_forward(base, alignment);

  auto alignment_shift  = static_cast<usize>(aligned_base - base);
  auto reservation_size = alignment_shift + size;
  
  if ((reservation_size + arena->offset) > arena->size) return nullptr;

  arena->offset += reservation_size;

  return aligned_base;
}

constexpr char * reserve_memory_unsafe (Memory_Arena *arena, usize size, usize alignment = alignof(void *)) {
  auto base         = arena->memory + arena->offset;
  auto aligned_base = align_forward(base, alignment);

  auto alignment_shift  = static_cast<usize>(aligned_base - base);
  auto reservation_size = alignment_shift + size;

  arena->offset += reservation_size;

  return aligned_base;
}

template <typename T>
constexpr T * reserve_struct (Memory_Arena *arena, usize alignment = alignof(T)) {
  return reinterpret_cast<T *>(reserve_memory(arena, sizeof(T), alignment));
}

template <typename T, typename ...Args>
constexpr T * push_struct (Memory_Arena *arena, usize alignment, Args &&...args) {
  auto object = reserve_struct<T>(arena, alignment);
  if (!object) return nullptr;

  *object = T { forward<Args>(args)... };

  return object;
}

template <typename T, typename ...Args>
constexpr T * push_struct (Memory_Arena *arena, Args &&...args) {
  return push_struct<T>(arena, alignof(T), forward<Args>(args)...);
}

template <typename T = char>
constexpr T * reserve_array  (Memory_Arena *arena, usize count, usize alignment = alignof(T)) {
  if (count == 0) return nullptr;
  return reinterpret_cast<T *>(reserve_memory(arena, sizeof(T) * count, alignment));
}

template <typename T = char>
constexpr T * reserve_array_unsafe (Memory_Arena *arena, usize count, usize alignment = alignof(T)) {
  if (count == 0) return nullptr;
  return reinterpret_cast<T *>(reserve_memory_unsafe(arena, sizeof(T) * count, alignment));
}

template <typename T = char>
constexpr Seq<T> reserve_seq (Memory_Arena *arena, usize count, usize alignment = alignof(T)) {
  if (count == 0) return {};
  return Seq(reinterpret_cast<T *>(reserve_memory(arena, sizeof(T) * count, alignment)), count);
}
