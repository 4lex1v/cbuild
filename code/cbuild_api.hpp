
#pragma once

#include "cbuild_api_template"
#include "cbuild_api_experimental"

#include "base.hpp"
#include "arena.hpp"
#include "list.hpp"
#include "strings.hpp"
#include "platform.hpp"

struct Target_Tracker;

typedef void (*Config_Crash_Handler) (u32 exit_code);

enum Target_Arch {
  Target_Arch_x86,
  Target_Arch_x64
};

struct Arguments {
  struct Argument {
    enum struct Type { Flag, Key_Value };

    Type type;
    const char *key;
    const char *value;
  };

  List<Argument> args;
};

using Argument = Arguments::Argument;

struct Target;

struct User_Defined_Command {
  String      name;
  Action_Type proc;
};

struct Project {
  mutable Memory_Arena arena;

  Toolchain_Configuration toolchain;

  bool rebuild_required;
  bool registry_disabled;

  List<User_Defined_Command> user_defined_commands;

  String    output_location;      // It's the value that the user can override from the configuration
  File_Path output_location_path; // The actual path resolved by the loader that should be used by the builder

  Target_Arch target_architecture = Target_Arch_x64;

  List<Target *> targets;
  u32            total_files_count;

  struct {
    List<File_Path> include_paths;

    List<String> compiler;
    List<String> archiver;
    List<String> linker;
  } global_options;

  const Arguments *args;

  struct {
    
  } hooks;
};

struct Target {
  /*
    I'd love to get rid of this limitation, as it's merely an internal convenience and doesn't carry much
    value to the user, but with having a static size for the name would simplify registry's layout, alleviating
    the need to move the memory manually. It's still worth to reconsider this at a later pointer, perhaps there's
    a better way to do this, without inflicting any limitation on the user?
  */
  constexpr static usize Max_Name_Limit = 32;

  enum struct Type {
    Static_Library,
    Shared_Library,
    Executable,
  };

  String name;
  Type   type;

  Project *project;
  Target_Tracker *tracker;

  /*
    These two field point to the Registry::Target_Info struct, but since I can't forward
    declare a nested struct, and I don't want to mess with headers, just defaulting to voids
    here. Otherwise it's safe to reinterpret_cast<Registry::Target_Info *>(...) in the builder.
   */
  void *info;
  void *last_info;

  struct {
    List<String> compiler;
    List<String> archiver;
    List<String> linker;
  } options;

  List<File_Path> files;
  List<File_Path> include_paths;
  List<String>    link_libraries;

  //File_Path output_file_path;

  List<const Target *> depends_on;
  List<const Target *> required_by;

  struct {
    Hook_Func on_linked;
  } hooks;
};

const char * get_argument_or_default (const Arguments *args, const char *key, const char *default_value);
