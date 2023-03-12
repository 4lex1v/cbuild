
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>

#include <cstdlib>
#include <cstring>

#include "runtime.hpp"
#include "driver.hpp"
#include "cbuild_api.hpp"
#include "toolchain.hpp"
#include "result.hpp"
#include "platform.hpp"

extern Platform_Info platform;

static Result<String> get_msvc_installation_path (Memory_Arena *arena) {
  usize env_value_reservation_size = 0;
  getenv_s(&env_value_reservation_size, nullptr, 0, "ProgramFiles(x86)");
  if (env_value_reservation_size == 0) return { Status_Code::Resource_Missing, "Couldn't resolve environment variable 'ProgramFiles(x86)'\n" };

  auto env_value_buffer = reserve_array<char>(arena, env_value_reservation_size);
  getenv_s(&env_value_reservation_size, env_value_buffer, env_value_reservation_size, "ProgramFiles(x86)");
  env_value_buffer[env_value_reservation_size] = '\0';

  auto command = format_string(arena,  R"("%\\Microsoft Visual Studio\\Installer\\vswhere.exe" -property installationPath)", String(env_value_buffer, env_value_reservation_size - 1));
  auto [vswhere_status, vs_path] = run_system_command(arena, command);
  if (vswhere_status != Status_Code::Success) {
    return { Status_Code::Resource_Missing, "Couldn't find Visual Studio on the system." };
  }

  assert(vs_path.length > 0);

  //print(arena, "Visual Studio installation found: %\n", vs_path);

  auto msvc_folder_query = format_string(arena, "%\\VC\\Tools\\MSVC\\*", vs_path);

  WIN32_FIND_DATA data;
  auto search_handle = FindFirstFile(msvc_folder_query, &data);
  if (search_handle == INVALID_HANDLE_VALUE) {
    return { Status_Code::Resource_Missing, "MSVC installation not found\n" };
  }

  auto split_version = [] (const char* version, int* major, int* minor, int* patch) {
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

  int major = 0, minor = 0, patch = 0;
  int max_major = 0, max_minor = 0, max_patch = 0;

  String path;

  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if ((strcmp(data.cFileName, ".")  == 0) ||
          (strcmp(data.cFileName, "..") == 0)) continue;

      split_version(data.cFileName, &major, &minor, &patch);

      if ((major > max_major) || (major == max_major && minor > max_minor) || (major == max_major && minor == max_minor && patch > max_patch)) {
        auto local = *arena;

        max_major = major;
        max_minor = minor;
        max_patch = patch;

        // Static cast is needed because cFileName is a static array with a given size. Casting to array, we'll
        // get the proper length of the value
        path = format_string(&local, "%\\VC\\Tools\\MSVC\\%\\bin\\Hostx64", vs_path, static_cast<char *>(data.cFileName));
      }
    }
  } while (FindNextFileA(search_handle, &data) != 0);

  arena->offset += (path.length + 1); // length doesn't include terminating null

  return path;
}

static Result<Toolchain_Configuration> load_llvm_toolchain (Memory_Arena *arena, bool force_clang = false) {
  String clang_path;
  String clang_cpp_path;
  String lld_link_path;
  String llvm_lib_path;

  {
    if (force_clang) {
      auto [where1_status, _clang_path] = run_system_command(arena, "where clang.exe");
      if (not where1_status) return { Status_Code::Resource_Missing, "Couldn't find clang.exe, please make sure it's added to the system's PATH.\n" };
          
      auto [where2_status, _clang_cpp_path] = run_system_command(arena, "where clang++.exe");
      if (not where2_status) return { Status_Code::Resource_Missing, "Couldn't find clang++.exe, please make sure it's added to the system's PATH.\n" };

      clang_path     = _clang_path;
      clang_cpp_path = _clang_cpp_path;
    }
    else {
      auto [where_status, clang_cl_path] = run_system_command(arena, "where clang-cl.exe");
      if (not where_status) return { Status_Code::Resource_Missing, "Couldn't find clang-cl.exe, please make sure it's added to the system's PATH.\n" };

      clang_path     = clang_cl_path;
      clang_cpp_path = clang_cl_path;
    }
  }

  {
    auto [where_status, _lld_link_path] = run_system_command(arena, "where lld-link.exe");
    if (not where_status) return { Status_Code::Resource_Missing, "Couldn't find lld-link.exe, please make sure it's added to the system's PATH.\n" };

    lld_link_path = _lld_link_path;
  }

  {
    auto [where_status, _llvm_lib_path] = run_system_command(arena, "where llvm-lib.exe");
    if (not where_status) return { Status_Code::Resource_Missing, "Couldn't find llvm-lib.exe, please make sure it's added to the system's PATH.\n" };

    llvm_lib_path = _llvm_lib_path;
  }
  
  return Toolchain_Configuration {
    .type              = force_clang ? Toolchain_Type_LLVM : Toolchain_Type_LLVM_CL,
    .c_compiler_path   = clang_path,
    .cpp_compiler_path = clang_cpp_path,
    .linker_path       = lld_link_path,
    .archiver_path     = llvm_lib_path,
  };
}

static Result<Toolchain_Configuration> load_gcc_toolchain (Memory_Arena *arena) {
  todo();
  return Status_Code::Success;
}

static Result<Toolchain_Configuration> load_msvc_x86_toolchain (Memory_Arena *arena) {
  auto msvc_path = get_msvc_installation_path(arena);
  check_status(msvc_path);

  auto cl_path   = format_string(arena, "%\\x86\\cl.exe", msvc_path);
  auto link_path = format_string(arena, "%\\x86\\link.exe", msvc_path);
  auto lib_path  = format_string(arena, "%\\x86\\lib.exe", msvc_path);

  return Toolchain_Configuration {
    .type              = Toolchain_Type_MSVC_X64,
    .c_compiler_path   = cl_path,
    .cpp_compiler_path = cl_path,
    .linker_path       = link_path,
    .archiver_path     = lib_path,
  };
}

static Result<Toolchain_Configuration> load_msvc_x64_toolchain (Memory_Arena *arena) {
  auto msvc_path = get_msvc_installation_path(arena);
  check_status(msvc_path);

  auto cl_path   = format_string(arena, "%\\x64\\cl.exe",   msvc_path);
  auto link_path = format_string(arena, "%\\x64\\link.exe", msvc_path);
  auto lib_path  = format_string(arena, "%\\x64\\lib.exe",  msvc_path);

  return Toolchain_Configuration {
    .type              = Toolchain_Type_MSVC_X64,
    .c_compiler_path   = cl_path,
    .cpp_compiler_path = cl_path,
    .linker_path       = link_path,
    .archiver_path     = lib_path,
  };
}

Result<Toolchain_Configuration> lookup_toolchain_by_type (Memory_Arena *arena, Toolchain_Type type) {
  use(Status_Code);

  assert(platform.type == Platform_Type::Win32); 

  switch (type) {
    case Toolchain_Type_MSVC_X86: return load_msvc_x86_toolchain(arena);
    case Toolchain_Type_MSVC_X64: return load_msvc_x64_toolchain(arena);
    case Toolchain_Type_LLVM:     return load_llvm_toolchain(arena, true);
    case Toolchain_Type_LLVM_CL:  return load_llvm_toolchain(arena);
    case Toolchain_Type_GCC:      return load_gcc_toolchain(arena);
  }
}

Result<Toolchain_Configuration> discover_toolchain (Memory_Arena *arena) {
  auto msvc_toolchain = lookup_toolchain_by_type(arena, Toolchain_Type_MSVC_X64);
  if (msvc_toolchain.status == Status_Code::Success) return msvc_toolchain;

  auto llvm_toolchain = lookup_toolchain_by_type(arena, Toolchain_Type_LLVM);
  if (llvm_toolchain.status == Status_Code::Success) return llvm_toolchain;

  auto gcc_toolchain = lookup_toolchain_by_type(arena, Toolchain_Type_GCC);
  if (gcc_toolchain.status == Status_Code::Success) return gcc_toolchain;

  return Status_Code::Resource_Missing;
}
