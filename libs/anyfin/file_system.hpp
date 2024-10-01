
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/array.hpp"
#include "anyfin/bit_mask.hpp"
#include "anyfin/memory.hpp"
#include "anyfin/meta.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/platform.hpp"

namespace Fin {

constexpr char get_path_separator();

constexpr String get_static_library_extension();
constexpr String get_shared_library_extension();
constexpr String get_executable_extension();
constexpr String get_object_extension();

using File_Path = String;

/*
  Construct a platform-dependent file path.
  Path separator is platform-dependent, i.e for Windows it's \, while for Unix systems - /.
 */
constexpr File_Path make_file_path (Memory_Arena &arena, String segment, Convertible_To<String> auto&&... other) {
  String segments[] { segment, other... };

  auto reservation_size = array_count_elements(segments);
  for (auto &s: segments) reservation_size += s.length;

  char *buffer = reserve<char>(arena, reservation_size);

  char *cursor = buffer;
  for (auto &segment: segments) {
    if (is_empty(segment)) continue;

    copy_memory(cursor, segment.value, segment.length);
    cursor[segment.length] = get_path_separator();

    cursor += segment.length + 1;
  }

  /*
    The last path separator would be replace with a 0 to terminate the string with a null-term.
    but the length should also be decremented to not include it.
  */
  cursor -= 1;
  *cursor = '\0';
  auto length = cursor - buffer;

#ifdef PLATFORM_WIN32
  for (usize idx = 0; idx < length; idx++) {
    if (buffer[idx] == '/') buffer[idx] = '\\';
  }
#endif

  return File_Path(buffer, length);
}

enum struct Resource_Type { File, Directory };

enum struct File_System_Flags: u64 {
  Write_Access   = fin_flag(1),
  Shared_Write   = fin_flag(2),
  Create_Missing = fin_flag(3),
  Always_New     = fin_flag(4),
  Force          = fin_flag(5),
};

/*
  Create a resource of a specified type on the file system under the specified path.
 */
static Sys_Result<void> create_resource (File_Path path, Resource_Type resource_type, Bit_Mask<File_System_Flags> flags = {});

static Sys_Result<void> create_file (File_Path path, Bit_Mask<File_System_Flags> flags = {}) {
  return create_resource(path, Resource_Type::File, flags);
}

fin_forceinline
static Sys_Result<void> create_directory (File_Path path, Bit_Mask<File_System_Flags> flags = {}) {
  return create_resource(path, Resource_Type::Directory, flags);
}

/*
  Check if the file pointed by the provided path exists on the file system.
  Returns true or false if the resource exists of not.
 */
static Sys_Result<bool> check_resource_exists (File_Path path, Option<Resource_Type> resource_type = {}) ;

/*
  Check if the provided path corresponds to an existing file on the file system.
 */
static Sys_Result<bool> check_file_exists (File_Path path) {
  return check_resource_exists(path, Resource_Type::File);
}

/*
  Check if the provided path corresponds to an existing directory on the file system.
 */
static Sys_Result<bool> check_directory_exists (File_Path path) {
  return check_resource_exists(path, Resource_Type::Directory);
}

/*
  Check whether the path refers to a file by ensuring it’s not a directory and that the file exists.
 */
static Sys_Result<bool> is_file (File_Path path);

/*
  Check if provided path has extension
 */
static bool has_file_extension (File_Path path);

/*
  Check whether the given path refers to a directory.
 */
static Sys_Result<bool> is_directory (File_Path path);

/*
  Delete resource pointed by the path.
  If the resource doesn't exist, just returns to the caller.
  If the resource does exist, attempt to delete it.
  If the resource is a directory that has content, it will be recursively deleted.
 */
static Sys_Result<void> delete_resource (File_Path path, Resource_Type resource_type) ;

/*
  Attempts to remove a file from the file system that corresponds to the given path.
 */
static Sys_Result<void> delete_file (File_Path path) {
  return delete_resource(path, Resource_Type::File);
}

/*
  Attempts to remove a directory with all its content from the file system.
 */
static Sys_Result<void> delete_directory (File_Path path) {
  return delete_resource(path, Resource_Type::Directory);
}

/*
  Extracts the resource (directory or file) name from the path, regardless if the actual
  resource exists on the file system or not. If it's a file and has an extension, the extension
  would be included.
 */
static Sys_Result<String> get_resource_name (File_Path path);

static Sys_Result<File_Path> get_absolute_path (Memory_Arena &arena, File_Path path);

static Sys_Result<File_Path> get_folder_path (Memory_Arena &arena, File_Path file);

static Sys_Result<File_Path> get_working_directory (Memory_Arena &arena);

static Sys_Result<void> set_working_directory (File_Path path);

static Sys_Result<void> for_each_file (File_Path directory, String extension, bool recursive, const Invocable<bool, File_Path> auto &func);

static Sys_Result<List<File_Path>> list_files (Memory_Arena &arena, File_Path directory, String extension = {}, bool recursive = false);

static Sys_Result<void> copy_file (File_Path from, File_Path to);

static Sys_Result<void> copy_directory (File_Path from, File_Path to);

struct File {
  void *handle;
  File_Path path;
};

static Sys_Result<File> open_file (File_Path path, Bit_Mask<File_System_Flags> flags = {});

static Sys_Result<void> close_file (File &file);

static Sys_Result<u64> get_file_size (const File &file); 

static Sys_Result<u64> get_file_id (const File &file);

static Sys_Result<void> write_bytes_to_file (File &file, Byte_Type auto *bytes, usize count);

static Sys_Result<void> write_bytes_to_file (File &file, String data) {
  return write_bytes_to_file(file, data.value, data.length);
}

template <usize N>
static Sys_Result<void> write_bytes_to_file (File &file, Byte_Type auto (&data)[N]) {
  return write_bytes_to_file(file, data, N);
}

static Sys_Result<void> read_bytes_into_buffer (File &file, u8 *buffer, usize bytes_to_read);

static Sys_Result<Array<u8>> get_file_content (Memory_Arena &arena, File &file);

static Sys_Result<void> reset_file_cursor (File &file);

static Sys_Result<u64> get_last_update_timestamp (const File &file);

struct File_Mapping {
  void *handle;
  
  char *memory;
  usize size;
};

static Sys_Result<File_Mapping> map_file_into_memory (const File &file);

static Sys_Result<void> unmap_file (File_Mapping &mapping);

}

#ifndef FIN_FILE_SYSTEM_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/file_system_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
