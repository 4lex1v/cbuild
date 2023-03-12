
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>

#include "core.hpp"
#include "list.hpp"
#include "platform.hpp"
#include "result.hpp"
#include "runtime.hpp"

File_Path::File_Path (const String &path): value { path.value }, length { path.length } {}

static Status_Code get_system_error () {
  auto error_code = GetLastError();

  LPSTR message = nullptr;
  FormatMessageA(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    0, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), message, 0, 0);

  return Status_Code { Status_Code::System_Error, message, error_code };
}

Result<File_Path> get_working_directory_path (Memory_Arena *arena) {
  use(Status_Code);
  
  auto buffer_size = GetCurrentDirectory(0, nullptr);
  if (buffer_size == 0) return get_system_error();
  
  auto buffer = reserve_array<char>(arena, buffer_size);

  auto path_length = GetCurrentDirectory(buffer_size, buffer);
  if (path_length == 0) return get_system_error();

  return File_Path(buffer, path_length);
}

Result<bool> check_file_exists (const File_Path *path) {
  auto allocation_size = GetFullPathName(path->value, 0, nullptr, nullptr);
  auto buffer          = reinterpret_cast<LPSTR>(_alloca(allocation_size));
  GetFullPathName(path->value, allocation_size, buffer, nullptr);

  const DWORD attributes = GetFileAttributes(path->value);
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;

  return !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

Result<bool> check_directory_exists (const File_Path *path) {
  auto attributes = GetFileAttributes(path->value);
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;

  return (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static Status_Code create_directory_recursive_inner (char *path) {
  auto attributes = GetFileAttributes(path);

  if ((attributes != INVALID_FILE_ATTRIBUTES) &&
      (attributes & FILE_ATTRIBUTE_DIRECTORY))
    return Status_Code::Success;

  auto separator = strrchr(path, '\\');
  if (separator) {
    *separator = '\0';
    auto status = create_directory_recursive_inner(path);
    *separator = '\\';

    if (not status) return status;
  }

  if (!CreateDirectory(path, NULL)) {
    if (GetLastError() == ERROR_ALREADY_EXISTS) return Status_Code::Success;
    return get_system_error();
  }

  return Status_Code::Success;
}

Status_Code create_directory_recursive (Memory_Arena *arena, const File_Path *path) {
  auto local = *arena;
  return create_directory_recursive_inner(const_cast<char *>(copy_string(&local, path->string_path()).value));
}

Status_Code create_directory (const File_Path *path) {
  use(Status_Code);
   
  if (!CreateDirectory(path->value, NULL)) {
    if (GetLastError() == ERROR_ALREADY_EXISTS) return Success;
    return get_system_error();
  }

  return Success;
}

Status_Code delete_file (const File_Path &path) {
  if (!DeleteFile(path.value)) {
    if (GetLastError() == ERROR_FILE_NOT_FOUND) return Status_Code::Success;
    return get_system_error();
  }

  return Status_Code::Success;
}

static Status_Code delete_directory_recursive (const File_Path &path) {
  use(Status_Code);

  char file_path[MAX_PATH];
  
  char directory_search_query[MAX_PATH];
  sprintf(directory_search_query, "%s\\*", path.value);
  
  WIN32_FIND_DATA data;
  auto search_handle = FindFirstFile(directory_search_query, &data);
  if (search_handle == INVALID_HANDLE_VALUE) return get_system_error();
  defer { FindClose(search_handle); };

  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if ((strcmp(data.cFileName, ".") != 0) &&
          (strcmp(data.cFileName, "..") != 0)) {
        auto length = sprintf(file_path, "%s\\%s", path.value, data.cFileName);
        check_status(delete_directory_recursive(File_Path(file_path, length)));
      }
    }
    else {
      auto length = sprintf(file_path, "%s\\%s", path.value, data.cFileName);
      check_status(delete_file(File_Path(file_path, length)));
    }
  } while (FindNextFile(search_handle, &data));

  if (!RemoveDirectory(path.value)) return get_system_error();

  return Status_Code::Success;
}

Status_Code delete_directory (const File_Path &path) {
  if (!RemoveDirectory(path.value)) {
    auto error_code = GetLastError();
    if      (error_code == ERROR_DIR_NOT_EMPTY)  return delete_directory_recursive(path);
    else if (error_code == ERROR_FILE_NOT_FOUND) return Status_Code::Success;

    return get_system_error();
  }

  return Status_Code::Success;
}

Status_Code load_shared_library (Shared_Library **library, const File_Path *library_file_path) {
  use(Status_Code);
  
  auto handle = LoadLibrary(library_file_path->value);
  if (handle == NULL) return get_system_error();

  *library = reinterpret_cast<Shared_Library *>(handle);

  return Success;
}

void unload_library (Shared_Library *library) {
  FreeLibrary((HMODULE) library);
}

void* load_symbol_from_library (const Shared_Library *library, const char *symbol_name) {
  return (void*) GetProcAddress((HMODULE) library, symbol_name);
}

Result<u64> get_last_update_timestamp (const File *file) {
  FILETIME last_update = {};
  GetFileTime(file->handle, 0, 0, &last_update);

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

Status_Code close_file (File *file) {
  if (CloseHandle(file->handle) == 0) return get_system_error();

  file->handle = nullptr;

  return Status_Code::Success;
}

void reset_file_cursor (File *file) {
  SetFilePointer(file->handle, 0, NULL, FILE_BEGIN);
}

Status_Code write_buffer_to_file (const File *file, const char *buffer, const size_t bytes_to_write) {
  use(Status_Code);
  
  DWORD bytes_written = 0;
  if (!WriteFile(file->handle, buffer, bytes_to_write, &bytes_written, NULL)) return get_system_error();
  if (bytes_written != bytes_to_write)                                        return get_system_error();

  return Success;
}

System_Command_Result run_system_command (Memory_Arena *arena, const char *command_line) {
  use(Status_Code);
  
  PROCESS_INFORMATION process  {};
  SECURITY_ATTRIBUTES security { .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE };

  HANDLE child_stdout_read, child_stdout_write;
  CreatePipe(&child_stdout_read, &child_stdout_write, &security, 0);

  STARTUPINFO info {
    .cb         = sizeof(STARTUPINFO),
    .dwFlags    = STARTF_USESTDHANDLES,
    .hStdOutput = child_stdout_write,
    .hStdError  = child_stdout_write,
  };

  if (CreateProcess(nullptr, const_cast<char *>(command_line), &security, &security, TRUE, 0, NULL, NULL, &info, &process) == false)
    return { .status = get_system_error() };

  CloseHandle(child_stdout_write);

  char *buffer = nullptr;
  usize offset = 0;

  if (get_remaining_size(arena) > 0) {
    buffer = get_memory_at_current_offset(arena);

    while (true) {
      auto space_left = get_remaining_size(arena);
      if (space_left == 0) break;

      DWORD bytes_read;
      ReadFile(child_stdout_read, buffer + offset, space_left, &bytes_read, NULL);

      if (bytes_read == 0) break;

      offset        += bytes_read;
      arena->offset += bytes_read;
    }

    buffer[offset] = '\0';

    /*
      Some commands may return the output with CRLF values at the end.
      We need to remove them when the return (i.e output) of this command is used as a part
      of a bigger string, for example when we resolve a path to the toolchain, we don't need
      CLRF values at the end. If a newline is needed, user should provide that manually.
    */
    if (offset > 1) {
      while (--offset > 0 && (buffer[offset] == 0x0D || buffer[offset] == 0x0A)) {
        buffer[offset] = '\0';
      }

      offset += 1;
    }
  }

  WaitForSingleObject(process.hProcess, INFINITE);

  DWORD return_value = 0;
  GetExitCodeProcess(process.hProcess, &return_value);

  CloseHandle(child_stdout_read);

  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);

  System_Command_Result result {
    .status = Success,
    .output = { offset ? buffer : nullptr, offset }
  };

  if (return_value != 0) {
    result.status = {
      System_Command_Error,
      format_string(arena, "Failed to execution command line '%', status: %\n", command_line, return_value),
      return_value
    };
  }

  return result;
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

Result<u64> get_file_size (const File *file) {
  use(Status_Code);
  
  LARGE_INTEGER file_size;
  if (GetFileSizeEx(file->handle, &file_size) == false) return get_system_error();

  return file_size.QuadPart;
}

Result<File_Path> get_file_path (Memory_Arena *arena, const File *file) {
  auto buffer = reserve_array<char>(arena, MAX_PATH);
  
  auto length = GetFinalPathNameByHandle(file->handle, buffer, MAX_PATH, FILE_NAME_NORMALIZED);
  if (length == 0) return get_system_error();

  arena->offset += (length + 1);

  return File_Path { buffer, length };
}

String get_file_name (const File_Path *path) {
  if (path->length == 0) return {};
  
  auto end = (path->value + (path->length - 1));
  if (*end == '\\' || *end == '/') return {};

  auto cursor = end;
  while (cursor >= path->value) {
    if (*cursor == '\\' || *cursor == '/') break;
    cursor -= 1;
  }

  if (cursor >= path->value) return { cursor + 1, usize(end - cursor) };

  return { path->value, path->length };
}

Result<File_Path> get_parent_folder_path (Memory_Arena *arena, const File *file) {
  auto absolute_path = get_absolute_path(arena, file->path.value);
  check_status(absolute_path);

  for (usize idx = absolute_path->length; idx > 0; idx--) {
    if (absolute_path->value[idx] == '/' || absolute_path->value[idx] == '\\') {
      auto parent_folder_path_part = String(absolute_path->value, idx);
    
      return File_Path(copy_string(arena, parent_folder_path_part));
    }
  }

  return Status_Code::Resource_Missing;
}

Result<u64> get_file_id (const File *file) {
  use(Status_Code);

  FILE_ID_INFO id_info;
  if (GetFileInformationByHandleEx(file->handle, FileIdInfo, &id_info, sizeof(id_info)) == false) return get_system_error();

  return *reinterpret_cast<u64 *>(id_info.FileId.Identifier);
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

  if (get_file_size(file) == 0) return File_Mapping {};
  
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

  auto buffer = reserve_memory(&local, MAX_PATH, alignof(char));

  char *file_name = nullptr;
  auto full_path_name_length = GetFullPathName(path, MAX_PATH, buffer, &file_name);
  if (full_path_name_length == 0) return get_system_error();

  int file_name_length = 0, extension_length = 0;
  while ((*(file_name + file_name_length) != '.') && (*(file_name + file_name_length) != '\0')) file_name_length++;
  while (*(file_name + file_name_length + extension_length) != '\0')                            extension_length++;

  u32 space_required = file_name_length + extension_length + 2;

  char *secondary_buffer = buffer + full_path_name_length + 1;
  if (space_required > (MAX_PATH - (full_path_name_length + 1))) {
    secondary_buffer = reserve_memory(&local, space_required, alignof(char));
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

Result<File_Path> get_absolute_path (Memory_Arena *arena, const char *path) {
  use(Status_Code);
  
  auto local = *arena;

  auto buffer = reserve_memory(&local, MAX_PATH, alignof(char));

  auto full_path_name_length = GetFullPathName(path, MAX_PATH, buffer, nullptr);
  if (full_path_name_length == 0) return get_system_error();

  arena->offset = local.offset;

  return File_Path { buffer, full_path_name_length };
}

Result<Thread> spawn_thread (Thread_Proc *proc, void *data) {
  use(Status_Code);

  DWORD thread_id;
  auto handle = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(proc), data, 0, &thread_id);
  if (!handle) return get_system_error();

  return Thread { reinterpret_cast<Thread::Handle *>(handle), thread_id };
}

void shutdown_thread (Thread *thread) {
  WaitForSingleObject(thread->handle, INFINITE);
  CloseHandle(thread->handle);
  thread->handle = nullptr;
}

u32 get_current_thread_id () {
  return GetCurrentThreadId();
}

Result<Semaphore> create_semaphore (u32 count) {
  LONG clamped = static_cast<LONG>(count);

  if      (count == 0)       clamped = 1;
  else if (count > LONG_MAX) clamped = LONG_MAX;

  auto handle = CreateSemaphore(nullptr, 0, clamped, nullptr);
  if (!handle) return get_system_error();
  
  return Semaphore { reinterpret_cast<Semaphore::Handle *>(handle) };
}

Status_Code destroy_semaphore (Semaphore *semaphore) {
  if (!CloseHandle(semaphore->handle)) return get_system_error();

  semaphore->handle = nullptr;

  return Status_Code::Success;
}

Result<u32> increment_semaphore (Semaphore *semaphore, u32 increment_value) {
  LONG previous;
  if (!ReleaseSemaphore(semaphore->handle, increment_value, &previous))
    return get_system_error();

  return previous;
}

Status_Code wait_for_semaphore_signal (Semaphore *semaphore) {
  if (WaitForSingleObject(semaphore->handle, INFINITE) == WAIT_FAILED) return get_system_error();
  return Status_Code::Success;
}

void raise_error_and_halt (String message) {
  Format_String format = "Unexpected fatal error occured: %. Terminating the application";

  // We need +1 space for the end line character the print will emplace.
  // I should probably change this API to make it more explicit and avoid the confusion later.
  auto buffer_size = format.reservation_size + message.length + 2;
  auto buffer      = new char[buffer_size]();

  Memory_Arena local { buffer, buffer_size };
  
  print(&local, std::move(format), message);
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

void list_files_in_directory (Memory_Arena *arena, List<File_Path> *list, const char *directory, const char *extension, bool recursive) {
  char path[MAX_PATH];

  auto [status, absolute_file_path] = get_absolute_path(arena, directory);
  if (absolute_file_path.value[absolute_file_path.length] == '\\') {
    absolute_file_path.length -= 1;
  }
  
  snprintf(path, MAX_PATH, "%.*s\\*.%s", (int)absolute_file_path.length, absolute_file_path.value, extension);

  WIN32_FIND_DATAA data;
  HANDLE search_handle;
  defer { FindClose(search_handle); };

  if ((search_handle = FindFirstFileA(path, &data)) != INVALID_HANDLE_VALUE) {
    do {
      if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0) {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          if (recursive) {
            snprintf(path, MAX_PATH, "%.*s\\%s", (int)absolute_file_path.length, absolute_file_path.value, data.cFileName);
            list_files_in_directory(arena, list, path, extension, recursive);
          }
        } else {
          // The reservation size includes the path separator and terminating null
          auto reservation_size = absolute_file_path.length + strlen(data.cFileName) + 2;
          auto file_path = reserve_array<char>(arena, reservation_size);

          snprintf(file_path, reservation_size, "%.*s\\%s", (int)absolute_file_path.length, absolute_file_path.value, data.cFileName);

          add(arena, list, File_Path(file_path));
        }
      }
    } while (FindNextFileA(search_handle, &data) != 0);
  }
}

u32 get_logical_cpu_count () {
  SYSTEM_INFO systemInfo;
  GetSystemInfo(&systemInfo);

  return systemInfo.dwNumberOfProcessors;
}
