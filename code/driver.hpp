
#pragma once

#include "base.hpp"

enum struct Platform_Type: u32 {
  Win32,
  Unix,
  Apple
};

struct Platform_Info {
  Platform_Type type;

  bool is_win32 () const { return type == Platform_Type::Win32; }
  bool is_unix  () const { return type == Platform_Type::Unix; }
  bool is_apple () const { return type == Platform_Type::Apple; }
};
