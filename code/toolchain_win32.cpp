
#include "anyfin/core/win32.hpp"
#include <shellapi.h>

#include "anyfin/base.hpp"

#include "anyfin/core/meta.hpp"
#include "anyfin/core/option.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/strings.hpp"
#include "anyfin/core/string_builder.hpp"

#include "anyfin/platform/platform.hpp"
#include "anyfin/platform/commands.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "toolchain.hpp"

static File_Path get_program_files_path (Memory_Arena &arena) {
  auto [has_failed, error, value] = get_env_var(arena, "ProgramFiles(x86)");
  if (has_failed) panic("Couldn't get the environment variable value for the key 'ProgramFiles(x86)' due to a system error: %\n", error);
  return value.take("No environment variable with a key 'ProgramFiles(x86)' found in process' environment\n");
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
static File_Path msvc_installation_path;
static String_View get_msvc_installation_path (Memory_Arena &arena) {
  if (msvc_installation_path) return msvc_installation_path;

  auto program_files_path = get_program_files_path(arena);

  auto command = format_string(arena, R"("%\Microsoft Visual Studio\Installer\vswhere.exe" -property installationPath)", program_files_path);
  auto [vs_path, vswhere_status] = run_system_command(arena, command)
    .take("Visual Studio install not found on the host system.");
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
  if (search_handle == INVALID_HANDLE_VALUE) trap("MSVC installation not found\n");

  int major = 0, minor = 0, patch = 0;
  int max_major = 0, max_minor = 0, max_patch = 0;

  String path;
  {
    /*
      In the loop we'll cycle through the folders with MSVC installation picking the highest number.
      `mark` copy exists here to avoid waisting space after the calculation, i.e since the overwrite
      should happen only if we got a bigger MSVC verion, we overwrite the previous value, thus when
      the loop exits, `mark` would point to the latest allocation, which will be back written into
      the arena.
     */
    auto mark = arena;

    do {
      if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (data.cFileName[0] == '.') continue;

        const auto file_name = String_View(cast_bytes(data.cFileName));

        split_version(data.cFileName, &major, &minor, &patch);

        if ((major > max_major) ||
            (major == max_major && minor > max_minor) ||
            (major == max_major && minor == max_minor && patch > max_patch)) {
          auto local = arena;

          max_major = major;
          max_minor = minor;
          max_patch = patch;

          path = concat_string(local, vs_path, "\\VC\\Tools\\MSVC\\", file_name);
          mark = local;
        }
      }
    } while (FindNextFileA(search_handle, &data) != 0);

    arena = mark;
  }

  if (is_empty(path)) trap("MSVC installation not found\n");

  msvc_installation_path = move(path);

  return msvc_installation_path;
}

static Option<Toolchain_Configuration> load_llvm_toolchain (Memory_Arena &arena, bool force_clang = false) {
  String clang_path;
  String clang_cpp_path;
  String lld_link_path;
  String llvm_lib_path;

  {
    if (force_clang) {
      char _clang_path[MAX_PATH];
      char _clang_cpp_path[MAX_PATH];

      // "Couldn't find clang.exe, please make sure it's added to the system's PATH.\n" 
      auto status = reinterpret_cast<usize>(FindExecutable("clang", NULL, _clang_path));
      if (status <= 32) return {};

      // "Couldn't find clang++.exe, please make sure it's added to the system's PATH.\n" 
      status = reinterpret_cast<usize>(FindExecutable("clang++", NULL, _clang_cpp_path));
      if (status <= 32) return {};
      
      clang_path     = String::copy(arena, _clang_path);
      clang_cpp_path = String::copy(arena, _clang_cpp_path);
    }
    else {
      char _clang_cl_path[MAX_PATH];
      
      // "Couldn't find clang-cl.exe, please make sure it's added to the system's PATH.\n" 
      auto status = reinterpret_cast<usize>(FindExecutable("clang-cl", NULL, _clang_cl_path));
      if (status <= 32) return {};

      clang_path     = String::copy(arena, _clang_cl_path);
      clang_cpp_path = clang_path;
    }
  }

  {
    char _lld_link_path[MAX_PATH];
    
    // "Couldn't find lld-link.exe, please make sure it's added to the system's PATH.\n" 
    auto status = reinterpret_cast<usize>(FindExecutable("lld-link", NULL, _lld_link_path));
    if (status <= 32) return {};

    lld_link_path = String::copy(arena, _lld_link_path);
  }

  {
    char _llvm_lib_path[MAX_PATH];
    
    // "Couldn't find llvm-lib.exe, please make sure it's added to the system's PATH.\n" 
    auto status = reinterpret_cast<usize>(FindExecutable("llvm-lib", NULL, _llvm_lib_path));
    if (status <= 32) return {};

    llvm_lib_path = String::copy(arena, _llvm_lib_path);
  }
  
  return Toolchain_Configuration {
    .type              = force_clang ? Toolchain_Type_LLVM : Toolchain_Type_LLVM_CL,
    .c_compiler_path   = clang_path,
    .cpp_compiler_path = clang_cpp_path,
    .linker_path       = lld_link_path,
    .archiver_path     = llvm_lib_path,
  };
}

static Option<Toolchain_Configuration> load_gcc_toolchain (Memory_Arena &arena) {
  trap("GCC Platform is not support on Win32 at this moment");

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

  assert(buffer[buffer_size - 1] == '\0');

  if (buffer[buffer_size - 2] == '\\') {
    buffer[buffer_size - 2] = '\0';
    buffer_size -= 1;
  }

  // buffer_size includes null terminator according to the Win32 spec.
  return File_Path(arena, buffer, buffer_size - 1);
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
    trap("Windows SDK is not found, please check that it's installed.\n"
         "CBuild checked Windows' registry at 'HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots\\KitsRoot10'\n"
         "and if it's not found in the registry the most command path is C:\\Program Files (x86)\\Windows Kits\\10\n."
         "If you do have SDK installed and any of the above entries exists on the host machine, this is likely a bug with a program.\n"
         "Please report this issue.\n");
  };

  File_Path windows_kits;
  {
    auto [found_in_registry, path] = lookup_windows_kits_from_registry(arena);
    if (!found_in_registry) {
      auto program_files_path = get_program_files_path(arena);
      auto kits_folder_path   = make_file_path(arena, program_files_path, "Windows Kits", "10");

      if (!check_directory_exists(kits_folder_path)) trap_no_sdk_found();

      windows_kits = move(kits_folder_path);
    }
  }

  auto folder_query = concat_string(arena, windows_kits, "\\Include\\*");

  WIN32_FIND_DATA data;
  auto search_handle = FindFirstFile(folder_query, &data);
  if (search_handle == INVALID_HANDLE_VALUE) trap_no_sdk_found();

  int max_minor = 0, max_revision = 0, max_build = 0;

  String path;
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (data.cFileName[0] == '.') continue;

      /*
        According to the SDK naming scheme it always starts with 10. for Win10+ and the same still holds for Win11.
       */
      assert(starts_with(data.cFileName, "10."));

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
    .version   = format_string(arena, "10.%.%.%", max_minor, max_revision, max_build),
  };
}

List<Env_Var> setup_system_sdk (Memory_Arena &arena, const Target_Arch architecture) {
  auto windows_sdk = find_windows_sdk(arena);
  auto msvc_path   = get_msvc_installation_path(arena);

  auto get_current_value = [&arena] (const char *name) -> Option<String> {
    auto [failed, _, value] = get_env_var(arena, name);
    if (!failed) return move(value);
    return opt_none;
  };

  List<Env_Var> previous(arena);

  {
    auto base_win_sdk_include_folder_path = concat_string(arena, windows_sdk.base_path, "\\Include\\", windows_sdk.version);

    auto local = arena;

    String_Builder includes { local };
    includes += concat_string(local, msvc_path, "\\include");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\cppwinrt");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\shared");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\ucrt");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\um");
    includes += concat_string(local, base_win_sdk_include_folder_path, "\\winrt");

    auto [defined, include_env_var] = get_current_value("INCLUDE");
    if (defined) {
      list_push(previous, Env_Var { String::copy(arena, "INCLUDE"), String::copy(arena, include_env_var) });
      includes += include_env_var;
    }

    SetEnvironmentVariable("INCLUDE", build_string_with_separator(local, includes, ';'));
  }

  {
    auto target_platform          = (architecture == Target_Arch_x86) ? String_View("x86") : String_View("x64");
    auto base_libpath_folder_path = concat_string(arena, windows_sdk.base_path, "\\Lib\\", windows_sdk.version);

    auto local = arena;

    String_Builder libpaths { local };
    libpaths += concat_string(local, msvc_path, "\\lib\\", target_platform);
    libpaths += concat_string(local, base_libpath_folder_path, "\\ucrt\\", target_platform);
    libpaths += concat_string(local, base_libpath_folder_path, "\\um\\",   target_platform);

    if (auto [defined, lib_env_var] = get_current_value("LIB"); defined) {
      list_push(previous, Env_Var { String::copy(arena, "LIB"), String::copy(arena, String_View(lib_env_var)) });
      libpaths += lib_env_var;
    }

    SetEnvironmentVariable("LIB", build_string_with_separator(local, libpaths, ';'));
  }

  return previous;
}

void reset_environment (const Slice<Env_Var> &env) {
  for (auto &[key, value]: env)
    if (!SetEnvironmentVariable(key.value, value.value)) [[unlikely]]
      panic("Failed to set the '%' envvar", String_View(key.value));
}
