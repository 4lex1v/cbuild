
#include "common_win32.hpp"

#include <cstdio>

#include "core.hpp"
#include "list.hpp"
#include "platform.hpp"
#include "result.hpp"
#include "runtime.hpp"

const char platform_path_separator = '\\';
const char platform_static_library_extension_name[] = "lib";
const char platform_shared_library_extension_name[] = "dll";
const char platform_executable_extension_name[]     = "exe";
const char platform_object_extension_name[]         = "obj";

File_Path::File_Path (const String &path): value { path.value }, length { path.length } {}


void set_working_directory (const File_Path &path) {
  SetCurrentDirectoryA(path.value);
}

bool check_file_exists (const File_Path *path) {
  auto allocation_size = GetFullPathName(path->value, 0, nullptr, nullptr);
  auto buffer          = reinterpret_cast<LPSTR>(_alloca(allocation_size));
  GetFullPathName(path->value, allocation_size, buffer, nullptr);

  const DWORD attributes = GetFileAttributes(path->value);
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;

  return !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

Status_Code delete_file (const File_Path &path) {
  if (!DeleteFile(path.value)) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) return Status_Code::Success;
    return get_system_error();
  }

  return Status_Code::Success;
}


Result<u64> get_last_update_timestamp (const File *file) {
  FILETIME last_update = {};
  if (!GetFileTime(file->handle, 0, 0, &last_update)) return get_system_error();

  ULARGE_INTEGER value;
  value.HighPart = last_update.dwHighDateTime;
  value.LowPart  = last_update.dwLowDateTime;

  return value.QuadPart;
}

Result<File> open_file (const File_Path *path, Bit_Mask<Open_File_Flags> flags) {
  use(Status_Code);
  using enum Open_File_Flags;

  auto access  = GENERIC_READ    | ((flags & Request_Write_Access) ? GENERIC_WRITE    : 0);
  auto sharing = FILE_SHARE_READ | ((flags & Allow_Shared_Writing) ? FILE_SHARE_WRITE : 0); 
  auto status  = (flags & Create_File_If_Not_Exists) ? OPEN_ALWAYS : OPEN_EXISTING;
  
  auto handle = CreateFile(path->value, access, sharing, NULL, status, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    if (status == OPEN_EXISTING && GetLastError() == ERROR_FILE_NOT_FOUND) return Resource_Missing;
    return get_system_error();
  }

  return File { reinterpret_cast<File_Handle *>(handle), *path };
}

void read_bytes_from_file_to_buffer (const File *file, char *buffer, const size_t bytes_to_read) {
  ReadFile(file->handle, buffer, bytes_to_read, NULL, NULL);
}

void retrieve_system_error (char **buffer, uint32_t * message_length) {
  const int error_code = GetLastError();
  *message_length = FormatMessageA(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    0,
    error_code,
    MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
    (LPSTR)buffer,
    0, 0);
}


Result<File_Path> get_file_path (Memory_Arena *arena, const File *file) {
  auto buffer = reserve_array<char>(arena, MAX_PATH);
  
  auto length = GetFinalPathNameByHandle(file->handle, buffer, MAX_PATH, FILE_NAME_NORMALIZED);
  if (length == 0) return get_system_error();

  arena->offset += (length + 1);

  return File_Path { buffer, length };
}




void platform_print_message (const String &message) {
  OutputDebugString(message.value);

  /*
    #OPTIMIZE:
      WriteFile(STD_OUTPUT) is not safe in multi-threaded env. I don't want to
      use synchronization mechanism though. Should look into this later for a
      proper logging solution.

   auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
   if (handle) WriteFile(handle, message.value, message.length, nullptr, nullptr);
   */
  printf("%s", message.value);
}

Result<File_Mapping> map_file_into_memory (const File *file) {
  use(Status_Code);

  if (*get_file_size(file) == 0) return File_Mapping {};
  
  auto handle = CreateFileMapping(file->handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (handle == nullptr) return get_system_error();

  auto memory = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
  if (memory == nullptr) { CloseHandle(handle); return get_system_error(); }

  auto [status, mapping_size] = get_file_size(file);
  if (!status) {
    UnmapViewOfFile(memory);
    CloseHandle(handle);
    return status;
  }

  return File_Mapping {
    .handle = handle,
    .memory = reinterpret_cast<char *>(memory),
    .size   = mapping_size
  };
}

Status_Code unmap_file (File_Mapping *mapping) {
  use(Status_Code);

  // Windows doesn't allow mapping empty files. I'm not treating this as an error, thus
  // it should be handled gracefully here as well.
  if (mapping->handle == nullptr) return Success;
  
  if (!UnmapViewOfFile(mapping->memory))  return get_system_error();
  if (!CloseHandle(mapping->handle))      return get_system_error();

  return Success;
}

Memory_Region reserve_virtual_memory (usize size) {
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);

  auto aligned_size = align_forward(size, system_info.dwPageSize);

  auto memory = VirtualAlloc(0, aligned_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  return Memory_Region { (char *) memory, aligned_size };
}

void free_virtual_memory (Memory_Region *region) {
  VirtualFree(region->memory, region->size, MEM_RELEASE);
}

Status_Code get_file_path_info (File_Path_Info *info, Memory_Arena *arena, const char *path) {
  use(Status_Code);

  Memory_Arena local = *arena;

  auto buffer = reserve(&local, MAX_PATH);

  char *file_name = nullptr;
  auto full_path_name_length = GetFullPathName(path, MAX_PATH, buffer, &file_name);
  if (full_path_name_length == 0) return get_system_error();

  int file_name_length = 0, extension_length = 0;
  while ((*(file_name + file_name_length) != '.') && (*(file_name + file_name_length) != '\0')) file_name_length++;
  while (*(file_name + file_name_length + extension_length) != '\0')                            extension_length++;

  u32 space_required = file_name_length + extension_length + 2;

  char *secondary_buffer = buffer + full_path_name_length + 1;
  if (space_required > (MAX_PATH - (full_path_name_length + 1))) {
    secondary_buffer = reserve(&local, space_required);
  }

  info->full_path        = buffer;
  info->full_path_length = full_path_name_length;

  info->name = reinterpret_cast<char *>(memcpy(secondary_buffer, file_name, file_name_length));
  secondary_buffer[file_name_length] = '\0';

  secondary_buffer += file_name_length + 1;

  /*
    Here the extension length would include the ., which I'd like to exclude from the value, thus adding +1 to offset the character.
   */
  if (extension_length > 1) {
    info->extension = reinterpret_cast<char *>(memcpy(secondary_buffer, file_name + file_name_length + 1, extension_length));
    secondary_buffer[extension_length] = '\0';
  }
    
  arena->offset += full_path_name_length + file_name_length + extension_length + 2;

  return Success;
}

u32 get_current_thread_id () {
  return GetCurrentThreadId();
}


void raise_error_and_halt (const char *location_tag, const char *message) {
  enum { buffer_size = 1024 };
  char buffer [buffer_size] {};
  Memory_Arena local { buffer, buffer_size };
  
  print(&local, "\n-------------\nFATAL ERROR:\n" "Where: %\n", location_tag);
  if (message && message[0] != '\0') print(&local, "%\n", message);

  print(&local, "-------------\n");
  
  exit(EXIT_FAILURE);
}

const char * get_path_to_executable (Memory_Arena *arena, const char *name) {
  auto local = *arena;

  auto buffer = reserve_array<char>(&local, MAX_PATH);
  auto path_length = SearchPath(nullptr, name, ".exe", MAX_PATH, buffer, nullptr);
  if (path_length == 0) return nullptr;

  arena->offset += path_length + 1;

  return buffer;
}

struct Performance_Counter {
  u64 frequency;
};

void copy_directory_content (Memory_Arena *arena, const File_Path &from, const File_Path &to) {
  WIN32_FIND_DATA find_file_data;

  auto local = *arena;

  auto search_query = format_string(arena, "%\\*", from);

  auto search_handle = FindFirstFile(search_query.value, &find_file_data);
  if (search_handle == INVALID_HANDLE_VALUE) return;
  defer { FindClose(search_handle); };

  do {
    auto scoped = local;

    auto file_to_move = format_string(&scoped, "%\\%", from, find_file_data.cFileName);
    auto destination  = format_string(&scoped, "%\\%", to,   find_file_data.cFileName);

    if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (lstrcmp(find_file_data.cFileName, ".") != 0 && lstrcmp(find_file_data.cFileName, "..") != 0) {
        CreateDirectory(destination.value, nullptr);
        copy_directory_content(&scoped, file_to_move, destination);
      }
    } else {
      CopyFile(file_to_move.value, destination.value, FALSE);
    }
  } while (FindNextFile(search_handle, &find_file_data) != 0);
}

Performance_Counter * create_performance_counter (Memory_Arena *arena) {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);

  auto counter = reserve_struct<Performance_Counter>(arena);
  counter->frequency = frequency.QuadPart;

  return counter;
}

u64 get_clock_timestamp (Performance_Counter *counter) {
  LARGE_INTEGER stamp;
  QueryPerformanceCounter(&stamp);

  return stamp.QuadPart;
}

u64 get_ellapsed_millis (Performance_Counter *counter, u64 from, u64 to) {
  u64 elapsed;
  elapsed = to - from;

  elapsed *= 1000;
  elapsed /= counter->frequency;

  return elapsed;
}


