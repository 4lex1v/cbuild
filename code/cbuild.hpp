
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/arena.hpp"
#include "anyfin/callsite.hpp"
#include "anyfin/result.hpp"
#include "anyfin/format.hpp"
#include "anyfin/string_converters.hpp"
#include "anyfin/process.hpp"
#include "anyfin/platform.hpp"

using namespace Fin;

#if !defined(TOOL_VERSION) || !defined(API_VERSION)
  #error "TOOL_VERSION and API_VERSION values must be defined at compile time"
#endif

void log (String message);

typedef void (*Panic_Handler) (u32 exit_code);
extern Panic_Handler panic_handler;

template <usize MEMORY_SIZE = 2048, String_Convertible... Args>
static void log (Format_String &&str_format, Args&&... args) {
  u8 stack_memory[MEMORY_SIZE];
  Memory_Arena arena { stack_memory };

  log(format_string(arena, move(str_format), forward<Args>(args)...));
}

template <usize MEMORY_SIZE = 2024, String_Convertible... Args>
[[noreturn]] static void panic (Format_String &&format, Args&&... args) {
  log(move(format), forward<Args>(args)...);

  panic_handler(1);
  __builtin_unreachable();
}

template <typename T>
static T && unwrap (Sys_Result<T> &&result, Callsite callsite = {}) {
  if (result.is_ok()) return move(result.value);
  panic("% - ERROR: Call failed due to the error: %\n", callsite, result.error.value);
}

template <typename T>
static T && unwrap (Sys_Result<T> &&result, String msg, Callsite callsite = {}) {
  if (result.is_ok()) return move(result.value);
  panic("% - %. ERROR: %\n", callsite, msg, result.error.value);
}

static void ensure (Sys_Result<void> &&result, Callsite callsite = {}) {
  if (result.is_error()) panic("% - ERROR: Call failed due to the error: %\n", callsite, result.error.value);
}

static void ensure (Sys_Result<void> &&result, String msg, Callsite callsite = {}) {
  if (result.is_error()) panic("% - %. ERROR: %\n", callsite, msg, result.error.value);
}

template <typename T>
static T && unwrap (Option<T> &&result, Callsite callsite = {}) {
  if (result.is_some()) return move(result.value);
  panic("% - ERROR: Call failed, no value returned\n", callsite);
}

template <typename T>
static T && unwrap (Option<T> &&result, String msg, Callsite callsite = {}) {
  if (result.is_some()) return move(result.value);
  panic("% - ERROR: %\n", callsite, msg);
}

