
#include "anyfin/win32.hpp"
#include <shellapi.h>

#include "anyfin/base.hpp"
#include "anyfin/meta.hpp"
#include "anyfin/option.hpp"
#include "anyfin/result.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/string_builder.hpp"
#include "anyfin/platform.hpp"
#include "anyfin/commands.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "toolchain.hpp"

static File_Path get_program_files_path (Memory_Arena &arena) {
  auto [error, value] = get_env_var(arena, "ProgramFiles(x86)");
  if (error) panic("Couldn't get the environment variable value for the key 'ProgramFiles(x86)' due to a system error: %\n", error.value);

  if (!value) panic("No environment variable with a key 'ProgramFiles(x86)' found in process' environment\n");

  return value.value;
}

static void split_version (const char* version, int* major, int* minor, int* patch) {
  usize index = 0;

  while (version[index] != '.') {
    *major = *major * 10 + (version[index] - '0');
    index++;
  }

  index++; // skip '.'

  while (version[index] != '.') {
    *minor = *minor * 10 + (version[index] - '0');
    index++;
  }

  index++; // skip '.'

  while (version[index] != '\0') {
    *patch = *patch * 10 + (version[index] - '0');
    index++;
  }
};

// Without linking with CRT, local static variables are not supported
static File_Path msvc_path;
static String get_msvc_installation_path (Memory_Arena &arena) {
  if (msvc_path) return msvc_path;

  auto program_files_path = get_program_files_path(arena);

  auto command = format_string(arena, R"("%\Microsoft Visual Studio\Installer\vswhere.exe" -property installationPath)", program_files_path);

  auto [error, vs_where_response] = run_system_command(arena, command);
  if (error) panic("Visual Studio install not found on the host system.");

  auto [vs_path, vswhere_status] = vs_where_response;
  if (vswhere_status != 0) {
    panic("MSVC lookup failed, vswhere.exe was completed with an error.\n"
          "Command: %\n"
          "Return status: %\n"
          "Output: %\n",
          command, vswhere_status, vs_path);
  }
    
  auto msvc_folder_query = concat_string(arena, vs_path, "\\VC\\Tools\\MSVC\\*");

  WIN32_FIND_DATA data;
  auto search_handle = FindFirstFile(msvc_folder_query, &data);
  if (search_handle == INVALID_HANDLE_VALUE) panic("MSVC installation not found\n");

  int max_major = 0, max_minor = 0, max_patch = 0;
  do {
    int major = 0, minor = 0, patch = 0;

    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (data.cFileName[0] == '.') continue;

      split_version(data.cFileName, &major, &minor, &patch);

      if ((major > max_major) ||
          (major == max_major && minor > max_minor) ||
          (major == max_major && minor == max_minor && patch > max_patch)) {
        max_major = major; max_minor = minor; max_patch = patch;
      }
    }
  } while (FindNextFileA(search_handle, &data) != 0);

  msvc_path = concat_string(arena, vs_path, "\\VC\\Tools\\MSVC\\", max_major, ".", max_minor, ".", max_patch);

  if (auto result = check_directory_exists(msvc_path); result.is_error() || !result.value)
    panic("Resolved MSVC path doesn't exist: %. If this folder does exists, this is likely a bug in CBuild.\n", msvc_path);

  return msvc_path;
}

static Option<Toolchain_Configuration> load_llvm_toolchain (Memory_Arena &arena, bool force_clang = false) {
  const auto get_executable = [&] (const char *name) -> String {
    char buffer[MAX_PATH];
    auto status = reinterpret_cast<usize>(FindExecutable(name, NULL, buffer));
    if (status <= 32) panic("Executable % not found, please make sure it's added to the system's PATH\n", name);
    return copy_string(arena, buffer);
  };
  
  return Toolchain_Configuration {
    .type              = force_clang ? Toolchain_Type_LLVM : Toolchain_Type_LLVM_CL,
    .c_compiler_path   = get_executable(force_clang ? "clang.exe" : "clang-cl.exe"),
    .cpp_compiler_path = get_executable(force_clang ? "clang++.exe" : "clang-cl.exe"),
    .linker_path       = get_executable("lld-link.exe"),
    .archiver_path     = get_executable("llvm-lib.exe"),
  };
}

static Option<Toolchain_Configuration> load_gcc_toolchain (Memory_Arena &arena) {
  panic("GCC Platform is not supported on Win32 at this moment\n");

  return {};
}

static Option<Toolchain_Configuration> load_msvc_x86_toolchain (Memory_Arena &arena) {
  auto msvc_path = get_msvc_installation_path(arena);

  auto cl_path   = concat_string(arena, msvc_path, "\\bin\\Hostx64\\x86\\cl.exe");
  auto link_path = concat_string(arena, msvc_path, "\\bin\\Hostx64\\x86\\link.exe");
  auto lib_path  = concat_string(arena, msvc_path, "\\bin\\Hostx64\\x86\\lib.exe");

  return Toolchain_Configuration {
    .type              = Toolchain_Type_MSVC_X86,
    .c_compiler_path   = cl_path,
    .cpp_compiler_path = cl_path,
    .linker_path       = link_path,
    .archiver_path     = lib_path,
  };
}

static Option<Toolchain_Configuration> load_msvc_x64_toolchain (Memory_Arena &arena) {
  auto msvc_path = get_msvc_installation_path(arena);

  auto cl_path   = concat_string(arena, msvc_path, "\\bin\\Hostx64\\x64\\cl.exe");
  auto link_path = concat_string(arena, msvc_path, "\\bin\\Hostx64\\x64\\link.exe");
  auto lib_path  = concat_string(arena, msvc_path, "\\bin\\Hostx64\\x64\\lib.exe");

  return Toolchain_Configuration {
    .type              = Toolchain_Type_MSVC_X64,
    .c_compiler_path   = cl_path,
    .cpp_compiler_path = cl_path,
    .linker_path       = link_path,
    .archiver_path     = lib_path,
  };
}

Option<Toolchain_Configuration> lookup_toolchain_by_type (Memory_Arena &arena, Toolchain_Type type) {
  switch (type) {
    case Toolchain_Type_MSVC_X86: return load_msvc_x86_toolchain(arena);
    case Toolchain_Type_MSVC_X64: return load_msvc_x64_toolchain(arena);
    case Toolchain_Type_LLVM:     return load_llvm_toolchain(arena, true);
    case Toolchain_Type_LLVM_CL:  return load_llvm_toolchain(arena);
    case Toolchain_Type_GCC:      return load_gcc_toolchain(arena);
  }
}

Option<Toolchain_Configuration> discover_toolchain (Memory_Arena &arena) {
  if (auto value = lookup_toolchain_by_type(arena, Toolchain_Type_MSVC_X64); value) return value;
  if (auto value = lookup_toolchain_by_type(arena, Toolchain_Type_LLVM);     value) return value;
  if (auto value = lookup_toolchain_by_type(arena, Toolchain_Type_GCC);      value) return value;

  return {};
}

static Option<File_Path> lookup_windows_kits_from_registry (Memory_Arena &arena) {
  DWORD buffer_size = MAX_PATH;
  auto buffer = reserve(arena, buffer_size);
  auto status = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", "KitsRoot10",
                             RRF_RT_REG_SZ, NULL, (PVOID)buffer, &buffer_size);
  if (status != ERROR_SUCCESS) return opt_none;

  fin_ensure(buffer[buffer_size - 1] == '\0');

  if (buffer[buffer_size - 2] == '\\') {
    buffer[buffer_size - 2] = '\0';
    buffer_size -= 1;
  }

  // buffer_size includes null terminator according to the Win32 spec.
  return File_Path(buffer, buffer_size - 1);
}

struct Windows_SDK {
  /*
    This is a partial path to the SDK that points to the kits folder, e.g C:\Program Files (x86)\Windows Kits\10.
   */
  File_Path base_path;
  String    version;
};

static Windows_SDK find_windows_sdk (Memory_Arena &arena) {
  const auto trap_no_sdk_found = [] {
    panic("Windows SDK is not found, please check that it's installed.\n"
         "CBuild checked Windows' registry at 'HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots\\KitsRoot10'\n"
         "and if it's not found in the registry the most command path is C:\\Program Files (x86)\\Windows Kits\\10\n."
         "If you do have SDK installed and any of the above entries exists on the host machine, this is likely a bug with a program.\n"
         "Please report this issue.\n");
  };

  File_Path windows_kits;
  {
    auto [found_in_registry, path] = lookup_windows_kits_from_registry(arena);
    if (found_in_registry) windows_kits = move(path);
    else {
      auto program_files_path = get_program_files_path(arena);
      windows_kits = move(make_file_path(arena, program_files_path, "Windows Kits", "10"));
    }
  }

  if (auto result = check_directory_exists(windows_kits);
      result.is_error() || !result.value)
    trap_no_sdk_found();

  auto folder_query = concat_string(arena, windows_kits, "\\Include\\*");

  WIN32_FIND_DATA data;
  auto search_handle = FindFirstFile(folder_query, &data);
  if (search_handle == INVALID_HANDLE_VALUE) panic("No installed Windows SDK found in %\n", windows_kits);

  int max_minor = 0, max_revision = 0, max_build = 0;

  String path;
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (data.cFileName[0] == '.') continue;

      /*
        According to the SDK naming scheme it always starts with 10. for Win10+ and the same still holds for Win11.
       */
      fin_ensure(starts_with(cast_bytes(data.cFileName), "10."));

      int minor = 0, revision = 0, build = 0;
      split_version(data.cFileName + 3, &minor, &revision, &build);

      if ((minor > max_minor) ||
          (minor == max_minor && revision > max_revision) ||
          (minor == max_minor && revision == max_revision && build > max_build)) {
        max_minor    = minor;
        max_revision = revision;
        max_build    = build;
      }
    }
  } while (FindNextFile(search_handle, &data));

  return Windows_SDK {
    .base_path = move(windows_kits),
    .version   = concat_string(arena, "10.", max_minor, ".", max_revision, ".", max_build),
  };
}

List<Env_Var> setup_system_sdk (Memory_Arena &arena, const Target_Arch architecture) {
  auto windows_sdk = find_windows_sdk(arena);
  auto msvc_path   = get_msvc_installation_path(arena);

  auto get_current_value = [&arena] (const char *name) -> Option<String> {
    auto [error, value] = get_env_var(arena, name);
    if (!error) return move(value);
    return opt_none;
  };

  List<Env_Var> previous(arena);

  {
    auto base_win_sdk_include_folder_path = concat_string(arena, windows_sdk.base_path, "\\Include\\", windows_sdk.version);

    auto [defined, include_env_var] = get_current_value("INCLUDE");
    if (defined) list_push(previous, Env_Var { copy_string(arena, "INCLUDE"), copy_string(arena, include_env_var) });

    auto local = arena;

    String_Builder includes { local };
    includes += concat_string(local, msvc_path, "\\include");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\cppwinrt");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\shared");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\ucrt");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\um");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\winrt");

    if (defined) includes += include_env_var;

    SetEnvironmentVariable("INCLUDE", build_string_with_separator(local, includes, ';'));
  }

  {
    auto target_platform          = (architecture == Target_Arch_x86) ? String("x86") : String("x64");
    auto base_libpath_folder_path = concat_string(arena, windows_sdk.base_path, "\\Lib\\", windows_sdk.version);

    auto [defined, lib_env_var] = get_current_value("LIB");
    if (defined) list_push(previous, Env_Var { copy_string(arena, "LIB"), copy_string(arena, String(lib_env_var)) });

    auto local = arena;

    String_Builder libpaths { local };
    libpaths += concat_string(local, msvc_path, "\\lib\\", target_platform);
    libpaths += concat_string(local, base_libpath_folder_path, "\\ucrt\\", target_platform);
    libpaths += concat_string(local, base_libpath_folder_path, "\\um\\",   target_platform);

    if (defined) libpaths += lib_env_var;

    SetEnvironmentVariable("LIB", build_string_with_separator(local, libpaths, ';'));
  }

  return previous;
}

void reset_environment (const List<Env_Var> &env) {
  for (auto &[key, value]: env)
    if (!SetEnvironmentVariable(key.value, value.value)) [[unlikely]]
      panic("Failed to set the '%' envvar", String(key.value));
}
