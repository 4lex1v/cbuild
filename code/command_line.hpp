
#pragma once

template <typename T> struct Seq;
template <typename T> struct Result;

struct Memory_Arena;

struct CLI_Flags {
  bool silenced;
};

struct CLI_Command {
  enum struct Value {
    Init,
    Build,
    Clean,
    Update,
    Version,
    Help,
    Dynamic
  };

  using enum Value;

  struct Init {
    enum Type { Cpp, C }; // Cpp should be the first value as the default 

    Type type;
  };

  struct Clean {
    bool all;
  };

  struct Build {
    const char *target_name;
    u32 builders_count;

    const char **arguments;
    int          count; 
  };

  struct Dynamic {
    const char  *command_name;

    const char **arguments;
    int          count;
  };

  Value type;
  union {
    struct Init    init;
    struct Build   build;
    struct Clean   clean;
    struct Dynamic dynamic;
  };

  bool operator == (Value _value) {
    return this->type == _value;
  }
};

struct CLI_Input {
  CLI_Flags   flags;
  CLI_Command command;
};

Result<CLI_Input> parse_command_line (Memory_Arena *arena, int argc, char **argv);

void print_usage (Memory_Arena *arena);
