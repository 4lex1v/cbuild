
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "result.hpp"

template <typename... Args>
static Status_Code get_system_error (Args... args) {
  auto error_code = GetLastError();

  const char *arg_array[] = { static_cast<const char *>(args)..., nullptr };

  LPSTR message = nullptr;
  FormatMessageA(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | (sizeof...(Args) > 0 ? FORMAT_MESSAGE_ARGUMENT_ARRAY : FORMAT_MESSAGE_IGNORE_INSERTS),
    nullptr, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPSTR)&message, 0, (va_list *)arg_array);

  return Status_Code { Status_Code::System_Error, message, error_code };
}
