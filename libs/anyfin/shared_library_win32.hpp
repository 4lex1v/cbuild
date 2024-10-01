
#define FIN_SHARED_LIBRARY_HPP_IMPL

#include "anyfin/win32.hpp"

#include "anyfin/shared_library.hpp"

namespace Fin {

static Sys_Result<Shared_Library *> load_shared_library (const File_Path &library_file_path) {
  auto handle = LoadLibrary(library_file_path.value);
  if (handle == nullptr) return get_system_error(library_file_path.value);

  return reinterpret_cast<Shared_Library *>(handle);
}

static Sys_Result<void> unload_library (Shared_Library &library) {
  if (!FreeLibrary((HMODULE) &library)) return get_system_error();

  return Ok();
}

template <typename T>
static Sys_Result<T *> lookup_symbol (const Shared_Library &library, const String &symbol_name) {
  const auto address = GetProcAddress((HMODULE) &library, symbol_name);
  if (!address) return get_system_error();

  return reinterpret_cast<T *>(address);
}

}
