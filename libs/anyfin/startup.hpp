
#pragma once

#include "anyfin/array.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/option.hpp"

namespace Fin {

/*
  Represents the CLI input argument, which is either an individual value, whatever it may be,
  or a pair, i.e a value in a form <key>=<value>.
 */
struct Startup_Argument {
  enum struct Type { Pair, Value };

  Type type;

  String key;
  String value;

  constexpr bool is_pair  () const { return type == Type::Pair; }
  constexpr bool is_value () const { return type == Type::Value; }
};

constexpr Option<String> get_value (const Iterable<Startup_Argument> auto &args, String key_name) {
  for (auto &arg: args) {
    if (arg.is_value()) continue;
    if (arg.key == key_name) return String(arg.value);
  }

  return opt_none;
}

static String get_program_name ();

static Array<Startup_Argument> get_startup_args (Memory_Arena &arena);

}

#ifndef FIN_STARTUP_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/startup_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
