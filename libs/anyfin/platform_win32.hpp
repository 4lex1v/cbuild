
#define FIN_PLATFORM_HPP_IMPL

#include "anyfin/win32.hpp"
#include <shellapi.h>

#include "anyfin/strings.hpp"
#include "anyfin/platform.hpp"

namespace Fin {

static Platform get_platform_type () { return Platform::Win32; }

static u32 get_system_error_code () {
  return GetLastError();
}

static System_Error get_system_error (Convertible_To<const char *> auto&&... args) {
  auto error_code = get_system_error_code();
  
  const auto flags    = FORMAT_MESSAGE_FROM_SYSTEM | (sizeof...(args) > 0 ? FORMAT_MESSAGE_ARGUMENT_ARRAY : FORMAT_MESSAGE_IGNORE_INSERTS);
  const auto language = MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT);

  const char *arg_array[] = { args..., nullptr };

  LPSTR message = nullptr;
  auto message_length = FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | (sizeof...(args) > 0 ? FORMAT_MESSAGE_ARGUMENT_ARRAY : FORMAT_MESSAGE_IGNORE_INSERTS),
    nullptr, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPSTR)&message, 0, (va_list *)arg_array);

  return System_Error { String(message, message_length), error_code };
}

static void destroy (System_Error error) {
  LocalFree((HLOCAL)error.details.value);
}

static u32 get_logical_cpu_count () {
  SYSTEM_INFO systemInfo;
  GetSystemInfo(&systemInfo);

  return systemInfo.dwNumberOfProcessors;
}

static Sys_Result<Option<String>> get_env_var (Memory_Arena &arena, String name) {
  auto reservation_size = GetEnvironmentVariable(name, nullptr, 0);
  if (!reservation_size) return get_system_error();

  auto env_value_buffer = reserve<char>(arena, reservation_size);
  auto env_value_length = GetEnvironmentVariable(name, env_value_buffer, reservation_size);
  if (!env_value_length) return get_system_error();

  fin_ensure(env_value_length == (reservation_size - 1));

  return Option(String(env_value_buffer, env_value_length));
}

static Sys_Result<Option<String>> find_executable (Memory_Arena &arena, String name) {
  char path[MAX_PATH] {};
  auto status = reinterpret_cast<usize>(FindExecutable(name.value, nullptr, path));

  switch (status) {
    case SE_ERR_FNF:          return Error(System_Error("The specified file was not found.", SE_ERR_FNF));
    case SE_ERR_PNF:          return Error(System_Error("The specified path is invalid.", SE_ERR_PNF));
    case SE_ERR_ACCESSDENIED: return Error(System_Error("The specified file cannot be accessed.", SE_ERR_ACCESSDENIED));
    case SE_ERR_OOM:          return Error(System_Error("The system is out of memory or resources.", SE_ERR_OOM));
    case SE_ERR_NOASSOC:      return Error(System_Error("There is no association for the specified file type with an executable file.", SE_ERR_NOASSOC));
  }

  // Not exactly sure if the above table fully covers all possible errors from FindExectuable
  if (status <= 32) return Error(System_Error("Executable wasn't found on the host system.", status));

  return Option(copy_string(arena, path, get_string_length(path)));
}

}
