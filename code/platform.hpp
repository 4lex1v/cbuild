
#pragma once

#include "base.hpp"
#include "core.hpp"
#include "strings.hpp"
#include "result.hpp"

struct Shared_Library;
struct Memory_Region;
struct Memory_Arena;
struct Status_Code;

template <typename T> struct Bit_Mask;
template <typename T> struct List;

#ifdef PLATFORM_WIN32
  #define platform_path_separator '\\'
#else
  #define platform_path_separator '/'
#endif

struct File_Path_Info {
  const char *full_path;
  usize       full_path_length;

  const char *name;
  const char *extension;
};

struct File_Path {
  const char *value  = nullptr;
  usize       length = 0;
  
  File_Path () = default;
  File_Path (const String &path);
  File_Path (const File_Path_Info &path)
    : value { path.full_path }, length { path.full_path_length } {}

  File_Path (const char *_value, usize _length)
    : value { _value }, length { _length } {}

  operator bool () const { return value != nullptr; }
  operator String () const { return String { value, length }; }

  String string_path () const { return String { value, length }; }
  bool is_empty () const { return value == nullptr; }
};

static bool check_extension (const File_Path &path, const String &extension) {
  String normalized = extension;

  if (normalized.value[0] == '.') [[unlikely]] {
    normalized.value  += 1; 
    normalized.length -= 1;
  }

  if (path.length < normalized.length) return false;

  const char *value = path.value + (path.length - normalized.length);
  for (usize idx = 0; idx < normalized.length; idx++) {
    if (value[idx] != normalized[idx]) return false;
  }

  return true;
}

template <typename T>
concept File_Path_Segment = std::is_same_v<std::decay_t<T>, File_Path> || std::convertible_to<T, String>;

template <File_Path_Segment... Segment>
Result<File_Path> make_file_path (Memory_Arena *arena, Segment&&... args) {
  use(Status_Code);
  
  auto get_reservation_size = [] (const String &segment) -> usize {
    return (segment.length == 0) ? 0 : segment.length + 1;
  };

  auto reservation_size = (get_reservation_size(args) + ...);

#ifdef PLATFORM_WIN32
  if (reservation_size > 260) return { Invalid_Value, "Constructed path surpases Windows path length limitation of 256 characters" };
#endif

  auto buffer = reserve_array<char>(arena, reservation_size, alignof(char));
  if (buffer == nullptr) return { Out_Of_Memory, "Not enough memory to construct the file path" };

  auto cursor = buffer;
  auto push_segment = [&cursor] (const String &segment) {
    if (segment.length == 0) return;
    
    memcpy(cursor, segment.value, segment.length);
    cursor[segment.length] = platform_path_separator;
    cursor += segment.length + 1;
  };

  (..., push_segment(args));

  buffer[reservation_size - 1] = '\0';

#ifdef PLATFORM_WIN32
  for (usize idx = 0; idx < reservation_size - 1; idx++) {
    if (buffer[idx] == '/') buffer[idx] = '\\';
  }
#endif

  return File_Path { buffer, reservation_size - 1 };
}

struct File {
  struct Handle;

  Handle    *handle;
  File_Path  path;
};

using File_Handle = File::Handle; // deprecated

Result<File_Path> get_working_directory_path (Memory_Arena *arena);
void set_working_directory (const File_Path &path);

void copy_directory_content (Memory_Arena *arena, const File_Path &from, const File_Path &to);

bool check_file_exists (const File_Path *path);

enum struct Open_File_Flags: u32 {
  Request_Write_Access      = flag(0),
  Allow_Shared_Writing      = flag(1),
  Create_File_If_Not_Exists = flag(2),
};

Result<File> open_file (const File_Path *path, Bit_Mask<Open_File_Flags> flags = {});

Status_Code get_file_path_info (File_Path_Info *info, Memory_Arena *arena, const char *file_path);

Result<File_Path> get_absolute_path (Memory_Arena *arena, const char *path);

String get_file_name (const File_Path *path);

void read_bytes_from_file_to_buffer (const File *file, char *buffer, const size_t bytes_to_read);

void reset_file_cursor (File *file);

Status_Code write_buffer_to_file (const File *file, const char *buffer, const size_t bytes_to_write);

bool check_directory_exists (const File_Path *path);

Status_Code create_directory (const File_Path *path);

Status_Code create_directory_recursive (Memory_Arena *arena, const File_Path *path);

Status_Code delete_directory (const File_Path &path);
Status_Code delete_file (const File_Path &path);

Status_Code close_file (File *file);

Result<u64> get_file_size (const File *file);

Result<File_Path> get_file_path (Memory_Arena *arena, const File *file);

Result<File_Path> get_parent_folder_path (Memory_Arena *arena, const File *file);

Result<u64> get_last_update_timestamp (const File *file);

Status_Code load_shared_library (Shared_Library **handle, const File_Path *library_file_path);

void unload_library (Shared_Library *library);

void * load_symbol_from_library (const Shared_Library *library, const char *symbol_name);

struct System_Command_Result {
  Status_Code status;
  String      output;
};

System_Command_Result run_system_command (Memory_Arena *arena, const char *command_line);

void retrieve_system_error (char **buffer, uint32_t *message_length);

Result<u64> get_file_id (const File *file);

struct File_Mapping {
  void *handle;
  
  char *memory;
  usize size;
};

Result<File_Mapping> map_file_into_memory (const File *file);

Status_Code unmap_file (File_Mapping *mapping);

Memory_Region reserve_virtual_memory (usize size);

void free_virtual_memory (Memory_Region *region);

using Thread_Proc = u32 (void *);

struct Thread {
  struct Handle;

  Handle *handle;
  u32     id;
};

Result<Thread> spawn_thread (Thread_Proc *proc, void *data);
void shutdown_thread (Thread *thread);

u32 get_logical_cpu_count ();

u32 get_current_thread_id ();

struct Semaphore {
  struct Handle;
  Handle *handle;
};

Result<Semaphore> create_semaphore (u32 count = static_cast<u32>(-1));
Status_Code destroy_semaphore (Semaphore *semaphore);

Result<u32> increment_semaphore (Semaphore *semaphore, u32 increment_value = 1);

Status_Code wait_for_semaphore_signal (Semaphore *sempahore);

struct RW_Lock;

void init_rw_lock (RW_Lock *lock);
void acquire_reader_lock (RW_Lock *lock);
void acquire_writer_lock (RW_Lock *lock);

void raise_error_and_halt (String message); 

const char * get_path_to_executable (Memory_Arena *arena, const char *name);

void platform_print_message (const String &message);

struct Performance_Counter;

Performance_Counter * create_performance_counter (Memory_Arena *arena);
u64 get_clock_timestamp (Performance_Counter *counter);
u64 get_ellapsed_millis (Performance_Counter *counter, u64 from, u64 to);

void list_files_in_directory (Memory_Arena *arena, List<File_Path> *list, const char *directory, const char *extension, bool recursive);

