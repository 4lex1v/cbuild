
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/arena.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/string_builder.hpp"
#include "anyfin/string_converters.hpp"
#include "anyfin/result.hpp"

namespace Fin {

enum struct Platform {
  Win32,
};

static Platform get_platform_type ();
static bool is_win32 () { return get_platform_type() == Platform::Win32; }

struct System_Error {
  String details;
  u32    error_code;
};

template <typename T>
using Sys_Result = Result<System_Error, T>;

static u32 get_system_error_code ();

static System_Error get_system_error (Convertible_To<const char *> auto&&... args);

static u32 get_logical_cpu_count (); 

static Sys_Result<Option<String>> get_env_var (Memory_Arena &arena, String name);

static Sys_Result<Option<String>> find_executable (Memory_Arena &arena, String name);

static auto to_string (const System_Error &error, Memory_Arena &arena) {
  auto string = get_memory_at_current_offset(arena);

  {
    const char msg[] = "system error(";
    auto msg_len = array_count_elements(msg) - 1;
    auto buffer = reserve<char>(arena, msg_len);

    copy_memory(buffer, msg, msg_len);
  }
  
  auto code_str = to_string(error.error_code, arena);
  if (code_str.length) [[likely]] arena.offset -= 1; // remove the terminating zero after rendering the numeric code

  {
    auto buffer = reserve<char>(arena, 3);
    copy_memory(buffer, "): ", 3);
  }

  {
    usize length = error.details.length;
    if      (ends_with(error.details, "\r\n")) length -= 2;
    else if (ends_with(error.details, "\n"))   length -= 1;

    auto buffer = reserve<char>(arena, length + 1);
    copy_memory(buffer, error.details.value, length);
    buffer[length] = '\0';
  }

  auto length = get_memory_at_current_offset(arena) - string - 1;

  return String(string, length);
}

}

#ifndef FIN_PLATFORM_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/platform_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
