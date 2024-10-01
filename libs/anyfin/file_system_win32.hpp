
#define FIN_FILE_SYSTEM_HPP_IMPL

#include "anyfin/arena.hpp"
#include "anyfin/option.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/meta.hpp"
#include "anyfin/defer.hpp"

#include "anyfin/file_system.hpp"

namespace Fin {

constexpr char get_path_separator() { return '\\'; }

constexpr String get_static_library_extension() { return "lib"; }
constexpr String get_shared_library_extension() { return "dll"; }
constexpr String get_executable_extension()     { return "exe"; }
constexpr String get_object_extension()         { return "obj"; }

static Sys_Result<void> create_resource (File_Path path, const Resource_Type resource_type, const Bit_Mask<File_System_Flags> flags) {
  switch (resource_type) {
    case Resource_Type::File: {
      using enum File_System_Flags;
      
      auto access  = GENERIC_READ    | ((flags & Write_Access) ? GENERIC_WRITE    : 0);
      auto sharing = FILE_SHARE_READ | ((flags & Shared_Write) ? FILE_SHARE_WRITE : 0); 

      auto handle = CreateFile(path.value, access, sharing, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      if (handle == INVALID_HANDLE_VALUE) return get_system_error();

      return Ok();
    }
    case Resource_Type::Directory: {
      if (CreateDirectory(path, nullptr)) return Ok();

      auto error_code = GetLastError();
      if (error_code == ERROR_ALREADY_EXISTS) return Ok();
      if (error_code != ERROR_PATH_NOT_FOUND) return get_system_error();

      if (!flags.is_set(File_System_Flags::Force)) return get_system_error();

      const auto create_recursive = [] (this auto self, char *path, usize length) -> Sys_Result<void> {
        auto attributes = GetFileAttributes(path);

        if ((attributes != INVALID_FILE_ATTRIBUTES) &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY))
          return Ok();

        auto separator = get_character_offset_reversed(path, length, '\\');
        if (separator) {
          *separator = '\0';
          fin_check(self(path, separator - path));
          *separator = '\\';
        }

        if (!CreateDirectory(path, nullptr)) {
          if (GetLastError() == ERROR_ALREADY_EXISTS) return Ok();
          return get_system_error();
        }

        return Ok();
      };

      fin_ensure(path.length < MAX_PATH);
      char path_buffer[MAX_PATH];
      copy_memory(path_buffer, path.value, path.length);
      path_buffer[path.length] = '\0';

      return create_recursive(path_buffer, path.length);
    }
  }
}

static Sys_Result<bool> check_resource_exists (File_Path path, Option<Resource_Type> resource_type) {
  const DWORD attributes = GetFileAttributes(path.value);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const auto error_code = get_system_error_code();
    if ((error_code == ERROR_FILE_NOT_FOUND) || (error_code == ERROR_PATH_NOT_FOUND)) return false;

    return get_system_error();
  }

  if (resource_type.is_none()) return true;

  switch (resource_type.value) {
    case Resource_Type::File:      return Ok(!(attributes  & FILE_ATTRIBUTE_DIRECTORY));
    case Resource_Type::Directory: return Ok(!!(attributes & FILE_ATTRIBUTE_DIRECTORY));
  }
}

static Sys_Result<void> delete_resource (File_Path path, Resource_Type resource_type) {
  switch (resource_type) {
    case Resource_Type::File: {
      if (!DeleteFile(path.value)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return Ok();
        return get_system_error();
      }

      return Ok();
    }
    case Resource_Type::Directory: {
      if (RemoveDirectory(path.value)) return Ok();

      auto error_code = GetLastError();
      if (error_code == ERROR_FILE_NOT_FOUND) return Ok();
      if (error_code == ERROR_PATH_NOT_FOUND) return Ok();
      if (error_code == ERROR_DIR_NOT_EMPTY)  {
        auto delete_recursive = [] (this auto self, File_Path path) -> Sys_Result<void> {
          char buffer[2048];
          Memory_Arena arena { buffer };

          auto directory_search_query = concat_string(arena, path, "\\*");
  
          WIN32_FIND_DATA data;
          auto search_handle = FindFirstFile(directory_search_query, &data);
          if (search_handle == INVALID_HANDLE_VALUE) return get_system_error();
          defer { FindClose(search_handle); };

          while (true) {
            auto scoped_arena = arena;

            auto file_name = String(cast_bytes(data.cFileName));
            if ((file_name != "." && file_name != "..")) {
              auto sub_path     = make_file_path(scoped_arena, path, file_name);
              auto is_directory = data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
              fin_check(is_directory ? self(sub_path) : delete_file(sub_path));
            }

            if (!FindNextFile(search_handle, &data)) {
              auto error_code = GetLastError();
              if (error_code == ERROR_NO_MORE_FILES) break;
              return get_system_error();
            }
          }

          if (!RemoveDirectory(path.value)) {
            fin_ensure(GetLastError() != ERROR_DIR_NOT_EMPTY);
            return get_system_error();
          }

          return Ok();
        };

        return delete_recursive(path); 
      }

      return get_system_error();
    }
  }
}

static Sys_Result<String> get_resource_name (File_Path path) {
  fin_ensure(path.length < MAX_PATH);

  int idx = path.length - 1;
  for (; idx >= 0; idx--) {
    if (path[idx] == '\\' || path[idx] == '/') {
      auto after_separator = idx + 1;
      return String(path.value + after_separator, path.length - after_separator);
    }
  }

  return path;
}

static Sys_Result<File_Path> get_absolute_path (Memory_Arena &arena, File_Path path) {
  auto full_path_name_length = GetFullPathName(path, 0, nullptr, nullptr);
  if (!full_path_name_length) return get_system_error();

  auto buffer = reserve<char>(arena, full_path_name_length);

  if (!GetFullPathName(path, full_path_name_length, buffer, nullptr)) return get_system_error();

  return File_Path(buffer, full_path_name_length - 1);
}

static bool is_absolute_path (File_Path path) {
  fin_ensure(!is_empty(path));

  if (path[0] == '.') return false;

  if (path.length > 2 && path[1] == ':'  && path[2] == '\\') return true;
  if (path.length > 1 && path[0] == '\\' && path[1] == '\\') return true;

  return false;
}

static Sys_Result<Resource_Type> get_resource_type (File_Path path) {
  const auto attrs = GetFileAttributes(path.value);
  if (attrs == INVALID_FILE_ATTRIBUTES) return get_system_error();

  return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? Resource_Type::Directory : Resource_Type::File;
};

static Sys_Result<File_Path> get_folder_path (Memory_Arena &arena, File_Path path) {
  char buffer[MAX_PATH];
  char *file_name_part = nullptr;

  auto full_path_length = GetFullPathName(path, MAX_PATH, buffer, &file_name_part);
  if (!full_path_length) return get_system_error();

  auto path_end                = file_name_part ? file_name_part : (buffer + full_path_length);
  auto folder_path_part_length = path_end - buffer;

  if (buffer[folder_path_part_length - 1] == '\\')
    folder_path_part_length -= 1;

  return copy_string(arena, String(buffer, folder_path_part_length));
}

static Sys_Result<File_Path> get_working_directory (Memory_Arena &arena) {
  auto buffer_size = GetCurrentDirectory(0, nullptr);
  if (buffer_size == 0) return Error(get_system_error());
  
  auto buffer = reserve<char>(arena, buffer_size);

  auto path_length = GetCurrentDirectory(buffer_size, buffer);
  if (!path_length) return get_system_error();

  return File_Path(buffer, path_length);
}

static Sys_Result<void> set_working_directory (File_Path path) {
  if (!SetCurrentDirectory(path.value)) return get_system_error();
  return Ok();
}

static Sys_Result<void> for_each_file (File_Path directory, String extension, bool recursive, const Invocable<bool, File_Path> auto &func) {
  auto run_visitor = [extension, recursive, func] (this auto self, File_Path directory) -> Sys_Result<bool> {
    char buffer[2048];
    Memory_Arena arena { buffer };

    WIN32_FIND_DATAA data;

    auto search_query  = concat_string(arena, directory, "\\*");
    auto search_handle = FindFirstFile(search_query, &data);
    if (search_handle == INVALID_HANDLE_VALUE) return get_system_error();
    defer { FindClose(search_handle); };

    do {
      auto local = arena;
      
      const auto file_name = String(cast_bytes(data.cFileName));
      if (file_name == "." || file_name == "..") continue;

      if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (!recursive) continue;

        auto [error, should_continue] = self(concat_string(local, directory, "\\", file_name));
        if (error)            return move(error.value);
        if (!should_continue) return false;
      }
      else {
        if (!ends_with(file_name, extension)) continue;
        if (!func(concat_string(local, directory, "\\", file_name))) return false;
      }
    } while (FindNextFileA(search_handle, &data) != 0);

    return Ok(true);
  };

  fin_check(run_visitor(directory));

  return Ok();
}

static Sys_Result<List<File_Path>> list_files (Memory_Arena &arena, File_Path directory, String extension, bool recursive) {
  List<File_Path> file_list { arena };

  auto list_recursive = [&] (this auto self, File_Path directory) -> Sys_Result<void> {
    WIN32_FIND_DATAA data;

    auto query = concat_string(arena, directory, "\\*");

    auto search_handle = FindFirstFile(query, &data);
    if (search_handle == INVALID_HANDLE_VALUE) return Error(get_system_error());
    defer { FindClose(search_handle); };

    do {
      auto local = arena;
      
      const auto file_name = String(cast_bytes(data.cFileName));
      if (file_name == "." || file_name == "..") continue;

      if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (recursive) fin_check(self(local, concat_string(local, directory, "\\", file_name)));
      }
      else {
        if (!ends_with(file_name, extension)) continue;
          
        auto file_path = concat_string(local, directory, "\\", file_name);
        if (!file_list.contains(file_path)) list_push(file_list, file_path);
      }
    } while (FindNextFileA(search_handle, &data) != 0);

    return Ok();
  };

  fin_check(list_recursive(directory));

  return Ok(move(file_list));
}

static Sys_Result<void> copy_file (File_Path from, File_Path to) {
  char buffer[2048];
  Memory_Arena arena { buffer };

  File_Path folder_path;
  {
    auto [sys_error, path] = get_folder_path(arena, to);
    if (sys_error) return move(sys_error.value);

    folder_path = path;
  }

  {
    auto [sys_error, result] = check_directory_exists(folder_path);
    if (sys_error) return move(sys_error.value);
    if (!result) create_directory(folder_path);
  }

  if (!CopyFile(from.value, to.value, FALSE)) {
    return get_system_error();
  }
    
  return Ok();
}

static Sys_Result<bool> is_file (File_Path path) {
  DWORD attributes = GetFileAttributes(path.value);
  if (attributes == INVALID_FILE_ATTRIBUTES) return get_system_error();
  if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
    
  return false;
}

static bool has_file_extension (File_Path path) {
  for (int i = path.length - 1; i >= 0; --i) {
    if (path.value[i] == '.') {
      if (i > 0 && i < path.length - 1) return true;
      break;
    }
  }

  return false;
}

static Sys_Result<bool> is_directory(File_Path path) {
  DWORD attributes = GetFileAttributes(path.value);
  if (attributes == INVALID_FILE_ATTRIBUTES) return get_system_error();
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) return true;

  return false;
}

static Sys_Result<void> copy_directory (File_Path from, File_Path to) {
  auto copy_recursive = [] (this auto self, File_Path from, File_Path to) -> Sys_Result<void> {
    char buffer[2048];
    Memory_Arena arena { buffer };

    auto search_query = concat_string(arena, from, "\\*");

    WIN32_FIND_DATA find_file_data;
    auto search_handle = FindFirstFile(search_query.value, &find_file_data);
    if (search_handle == INVALID_HANDLE_VALUE) return get_system_error();
    defer { FindClose(search_handle); };

    while (true) {
      auto scoped_arena = arena;

      auto file_name = String(cast_bytes(find_file_data.cFileName));
      if (file_name != "." && file_name != "..") {
        auto file_to_move = make_file_path(scoped_arena, from, file_name);
        auto destination  = make_file_path(scoped_arena, to,   file_name);

        if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          if (!CreateDirectory(destination, nullptr)) return get_system_error();
          fin_check(self(file_to_move, destination));
        }
        else {
          if (!CopyFile(file_to_move.value, destination.value, FALSE)) return get_system_error();
        }
      }

      if (!FindNextFile(search_handle, &find_file_data)) {
        auto error_code = GetLastError();
        if (error_code == ERROR_NO_MORE_FILES) break;
        return get_system_error();
      }
    }

    return Ok();
  };

  fin_check(create_directory(to));

  return copy_recursive(from, to);
}

static Sys_Result<File> open_file (File_Path path, Bit_Mask<File_System_Flags> flags) {
  using enum File_System_Flags;

  auto access  = GENERIC_READ    | ((flags & Write_Access) ? GENERIC_WRITE    : 0);
  auto sharing = FILE_SHARE_READ | ((flags & Shared_Write) ? FILE_SHARE_WRITE : 0); 

  auto creation = OPEN_EXISTING;
  if      (flags & Create_Missing) creation = OPEN_ALWAYS;
  else if (flags & Always_New)     creation = CREATE_ALWAYS;
  
  auto handle = CreateFile(path.value, access, sharing, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) return get_system_error();

  return File { handle, move(path) };
}

static Sys_Result<void> close_file (File &file) {
  if (CloseHandle(file.handle) == 0) return get_system_error();
  file.handle = nullptr;
  return Ok();
}

static Sys_Result<u64> get_file_size (const File &file) {
  LARGE_INTEGER file_size;
  if (GetFileSizeEx(file.handle, &file_size) == false) return get_system_error();

  return file_size.QuadPart;
}

static Sys_Result<u64> get_file_id (const File &file) {
  BY_HANDLE_FILE_INFORMATION info;
  if (!GetFileInformationByHandle(file.handle, &info))
    return get_system_error();

  u64 file_id  = info.nFileIndexLow;
  file_id     |= (static_cast<u64>(info.nFileIndexHigh) << 32);
  
  return file_id;
}

static Sys_Result<void> write_bytes_to_file (File &file, Byte_Type auto *bytes, usize count) {
  DWORD total_bytes_written = 0;
  while (total_bytes_written < count) {
    DWORD bytes_written = 0;
    if (!WriteFile(file.handle, bytes + total_bytes_written, 
                   count - total_bytes_written, &bytes_written, nullptr)) {
      return get_system_error();
    }

    if (bytes_written == 0) {
      // No more bytes were written, could be a device error or a full disk.
      return get_system_error();  // or a custom error indicating partial write
    }

    total_bytes_written += bytes_written;
  }

  return Ok();
}

static Sys_Result<void> read_bytes_into_buffer (File &file, u8 *buffer, usize bytes_to_read) {
  fin_ensure(buffer);
  fin_ensure(bytes_to_read > 0);

  usize offset = 0;
  while (offset < bytes_to_read) {
    DWORD bytes_read = 0;
    if (!ReadFile(file.handle, buffer + offset, bytes_to_read - offset, &bytes_read, nullptr)) {
      return get_system_error();
    }

    offset += bytes_read;
  }

  return Ok();
}

static Sys_Result<Array<u8>> get_file_content (Memory_Arena &arena, File &file) {
  fin_check(reset_file_cursor(file));

  auto [sys_error, file_size] = get_file_size(file);
  if (sys_error)  return move(sys_error.value);
  if (!file_size) return Ok(Array<u8> {});

  auto buffer = reserve_array<u8>(arena, file_size, alignof(u8));

  usize offset = 0;
  while (offset < file_size) {
    DWORD bytes_read = 0;
    if (!ReadFile(file.handle, buffer.values + offset, file_size - offset, &bytes_read, NULL))
      return get_system_error();

    offset += bytes_read;
  }

  return buffer;
}

static Sys_Result<void> reset_file_cursor (File &file) {
  if (SetFilePointer(file.handle, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    return get_system_error();

  return Ok();
}

static Sys_Result<u64> get_last_update_timestamp (const File &file) {
  FILETIME last_update = {};
  if (!GetFileTime(file.handle, 0, 0, &last_update)) return get_system_error();

  ULARGE_INTEGER value;
  value.HighPart = last_update.dwHighDateTime;
  value.LowPart  = last_update.dwLowDateTime;

  return static_cast<u64>(value.QuadPart);
}

static Sys_Result<File_Mapping> map_file_into_memory (const File &file) {
  auto [sys_error, mapping_size] = get_file_size(file);
  if (sys_error) return move(sys_error.value);
  if (mapping_size == 0) return File_Mapping {};
  
  auto handle = CreateFileMapping(file.handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!handle) return get_system_error();

  auto memory = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
  if (!memory) {
    CloseHandle(handle);
    return get_system_error();
  }

  return File_Mapping {
    .handle = handle,
    .memory = reinterpret_cast<char *>(memory),
    .size   = mapping_size
  };
}

static Sys_Result<void> unmap_file (File_Mapping &mapping) {
  // Windows doesn't allow mapping empty files. I'm not treating this as an error, thus
  // it should be handled gracefully here as well.
  if (!mapping.handle) return Ok();
  
  if (!UnmapViewOfFile(mapping.memory))  return get_system_error();
  if (!CloseHandle(mapping.handle))      return get_system_error();

  return Ok();
}

}
