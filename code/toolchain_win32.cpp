
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include "anyfin/base.hpp"

#include "anyfin/core/meta.hpp"
#include "anyfin/core/option.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/seq.hpp"
#include "anyfin/core/strings.hpp"
#include "anyfin/core/string_builder.hpp"

#include "anyfin/platform/platform.hpp"
#include "anyfin/platform/commands.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "toolchain.hpp"

static File_Path get_program_files_path (Memory_Arena &arena) {
  auto env_value_reservation_size = GetEnvironmentVariable("ProgramFiles(x86)", nullptr, 0);
  if (env_value_reservation_size == 0) panic("Couldn't load ProgramFiles(x86) path from Windows' registry.");

  auto env_value_buffer = reinterpret_cast<char *>(reserve_memory(arena, env_value_reservation_size));
  if (!GetEnvironmentVariable("ProgramFiles(x86)", env_value_buffer, env_value_reservation_size))
    panic("Couldn't load ProgramFiles(x86) path from Windows' registry.");

  return File_Path(arena, env_value_buffer, env_value_reservation_size - 1);
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
static Option<File_Path> msvc_installation_path;
static Option<String_View> get_msvc_installation_path (Memory_Arena &arena) {
  if (msvc_installation_path) return Option(String_View(*msvc_installation_path));

  auto program_files_path = get_program_files_path(arena);

  auto command = format_string(arena,  R"("%\\Microsoft Visual Studio\\Installer\\vswhere.exe" -property installationPath)", program_files_path);
  auto [vswhere_status, vs_path] = run_system_command(arena, command)
    .take("Visual Studio install not found on the host system.");
    
  auto msvc_folder_query = format_string(arena, "%\\VC\\Tools\\MSVC\\*", vs_path);

  WIN32_FIND_DATA data;
  auto search_handle = FindFirstFile(msvc_folder_query, &data);
  if (search_handle == INVALID_HANDLE_VALUE) trap("MSVC installation not found\n");

  int major = 0, minor = 0, patch = 0;
  int max_major = 0, max_minor = 0, max_patch = 0;

  String path;
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if ((strcmp(data.cFileName, ".")  == 0) ||
          (strcmp(data.cFileName, "..") == 0)) continue;

      split_version(data.cFileName, &major, &minor, &patch);

      if ((major > max_major) ||
          (major == max_major && minor > max_minor) ||
          (major == max_major && minor == max_minor && patch > max_patch)) {
        auto local = arena;

        max_major = major;
        max_minor = minor;
        max_patch = patch;

        path = format_string(local, "%\\VC\\Tools\\MSVC\\%", vs_path, String_View(data.cFileName));
      }
    }
  } while (FindNextFileA(search_handle, &data) != 0);

  arena.offset += (path.length + 1); // length doesn't include terminating null

  msvc_installation_path = Option(move(path));

  return Option(String_View(*msvc_installation_path));
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
  auto [is_defined, msvc_path] = get_msvc_installation_path(arena);
  if (!is_defined) return {};

  auto cl_path   = format_string(arena, "%\\bin\\Hostx64\\x86\\cl.exe",   msvc_path);
  auto link_path = format_string(arena, "%\\bin\\Hostx64\\x86\\link.exe", msvc_path);
  auto lib_path  = format_string(arena, "%\\bin\\Hostx64\\x86\\lib.exe",  msvc_path);

  return Toolchain_Configuration {
    .type              = Toolchain_Type_MSVC_X86,
    .c_compiler_path   = cl_path,
    .cpp_compiler_path = cl_path,
    .linker_path       = link_path,
    .archiver_path     = lib_path,
  };
}

static Option<Toolchain_Configuration> load_msvc_x64_toolchain (Memory_Arena &arena) {
  auto [is_defined, msvc_path] = get_msvc_installation_path(arena);
  if (!is_defined) return {};

  auto cl_path   = format_string(arena, "%\\bin\\Hostx64\\x64\\cl.exe",   msvc_path);
  auto link_path = format_string(arena, "%\\bin\\Hostx64\\x64\\link.exe", msvc_path);
  auto lib_path  = format_string(arena, "%\\bin\\Hostx64\\x64\\lib.exe",  msvc_path);

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
  HKEY hKey;

  DWORD buffer_size = MAX_PATH;
  auto buffer = reinterpret_cast<char *>(reserve_memory(arena, buffer_size, alignof(char)));

  auto status = RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", "KitsRoot10",
                             RRF_RT_REG_SZ, NULL, (PVOID)buffer, &buffer_size);
  if (status != ERROR_SUCCESS) return {};

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

static Option<Windows_SDK> find_windows_sdk (Memory_Arena &arena) {
  auto windows_kits = lookup_windows_kits_from_registry(arena);
  if (!windows_kits) return {};
  
  auto program_files_path = get_program_files_path(arena);
  auto sdks_folder        = make_file_path(arena, program_files_path, "Windows Kits", "10");

  if (!check_resource_exists(sdks_folder, Resource_Type::Directory)) return {};

  auto folder_query = format_string(arena, "%\\Include\\*", sdks_folder);

  WIN32_FIND_DATA data;
  auto search_handle = FindFirstFile(folder_query, &data);
  // "No installed Windows SDK were found." 
  if (search_handle == INVALID_HANDLE_VALUE) return {};

  int max_minor = 0, max_revision = 0, max_build = 0;

  String path;
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (data.cFileName[0] == '.') continue;

      /*
        According to the SDK naming scheme it always starts with 10. for Win10+ and the same still holds for Win11.
       */
      assert(strcmp(data.cFileName, "10.") != 0);

      int minor = 0, revision = 0, build = 0;
      split_version(data.cFileName + 3, &minor, &revision, &build);

      if ((minor > max_minor) ||
          (minor == max_minor && revision > max_revision) ||
          (minor == max_minor && revision == max_revision && build > max_build)) {
        max_minor = minor;
        max_revision = revision;
        max_build = build;
      }
    }
  } while (FindNextFile(search_handle, &data));

  return Windows_SDK {
    .base_path = move(sdks_folder),
    .version   = format_string(arena, "10.%.%.%", max_minor, max_revision, max_build),
  };
}

List<Env_Var> setup_system_sdk (Memory_Arena &arena, const Target_Arch architecture) {
  auto sdk = find_windows_sdk(arena);
  if (!sdk) return {};

  auto msvc = get_msvc_installation_path(arena);
  if (!msvc) return {};

  auto get_current_value = [&arena] (const char *name) -> Option<String> {
    auto value_size = GetEnvironmentVariable(name, nullptr, 0);
    if (!value_size) return {};
    
    auto buffer = reinterpret_cast<char *>(reserve_memory(arena, value_size));
    if (!GetEnvironmentVariable(name, buffer, value_size)) return {};

    return Option(String(arena, buffer, value_size));
  };

  List<Env_Var> previous(arena);

  get_current_value("INCLUDE").handle_value([&] (auto &include_env_var) {
    list_push(previous, Env_Var { String::copy(arena, "INCLUDE"), String::copy(arena, String_View(include_env_var)) });

    auto base_win_sdk_include_folder_path = format_string(arena, "%\\Include\\%", sdk->base_path, sdk->version);

    auto local = arena;

    String_Builder includes { local };
    {
      includes += format_string(local, "%\\include",  *msvc);
      includes += format_string(local, "%\\cppwinrt", base_win_sdk_include_folder_path);
      includes += format_string(local, "%\\shared",   base_win_sdk_include_folder_path);
      includes += format_string(local, "%\\ucrt",     base_win_sdk_include_folder_path);
      includes += format_string(local, "%\\um",       base_win_sdk_include_folder_path);
      includes += format_string(local, "%\\winrt",    base_win_sdk_include_folder_path);
      includes += include_env_var;
    }

    SetEnvironmentVariable("INCLUDE", build_string_with_separator(local, includes, ';'));
  });

  get_current_value("LIB").handle_value([&] (auto &lib_env_var) {
    list_push(previous, Env_Var { String::copy(arena, "LIB"), String::copy(arena, String_View(lib_env_var)) });

    auto target_platform          = String_View((architecture == Target_Arch_x86) ? "x86" : "x64");
    auto base_libpath_folder_path = format_string(arena, "%\\Lib\\%", sdk->base_path, sdk->version);

    auto local = arena;

    String_Builder libpaths { local };
    {
      libpaths += format_string(local, "%\\lib\\%", *msvc, target_platform);
      libpaths += format_string(local, "%\\ucrt\\%", base_libpath_folder_path, target_platform);
      libpaths += format_string(local, "%\\um\\%", base_libpath_folder_path, target_platform);
      libpaths += lib_env_var;
    }

    SetEnvironmentVariable("LIB", build_string_with_separator(local, libpaths, ';'));
  });

  return previous;
}

void reset_environment (const Slice<Env_Var> &env) {
  for (auto &[key, value]: env)
    if (!SetEnvironmentVariable(key.value, value.value)) [[unlikely]]
      panic("Failed to set the '%' envvar", String_View(key.value));
}
