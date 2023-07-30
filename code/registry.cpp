
#include "registry.hpp"

Status_Code load_registry (Registry *registry, Memory_Arena *arena, const File_Path *registry_file_path) {
  using enum Open_File_Flags;
  use(Status_Code);

  auto &records = registry->records;

  auto registry_file = open_file(registry_file_path, Request_Write_Access | Create_File_If_Not_Exists);
  check_status(registry_file);

  registry->registry_file = registry_file;

  auto file_size = get_file_size(&registry_file);
  if (file_size == 0) return Success;

  auto mapping = map_file_into_memory(&registry_file);
  check_status(mapping);

  auto buffer        = reinterpret_cast<u8 *>(mapping->memory);
  auto buffer_cursor = buffer;

  auto read_header_field = [&buffer_cursor] <typename T> (T *field) {
    *field         = *reinterpret_cast<T *>(buffer_cursor);
    buffer_cursor += sizeof(T);
  };

  auto set_field = [&buffer_cursor] <typename T> (T *field, usize count, usize align = 0) {
    if (align > 0) buffer_cursor = align_forward(buffer_cursor, align);

    auto value_size = sizeof(remove_ptr_t<T>) * count;

    *field         = reinterpret_cast<T>(buffer_cursor);
    buffer_cursor += value_size;
  };

  records.registry_file_mapping = mapping;

  read_header_field(&records.header.version);
  read_header_field(&records.header.targets_count);
  read_header_field(&records.header.aligned_total_files_count);
  read_header_field(&records.header.dependencies_count);

  buffer_cursor += sizeof(records.header._reserved);

  set_field(&records.targets,            records.header.targets_count);
  set_field(&records.files,              records.header.aligned_total_files_count, 32);
  set_field(&records.file_records,       records.header.aligned_total_files_count);
  set_field(&records.dependencies,       records.header.dependencies_count, 32);
  set_field(&records.dependency_records, records.header.dependencies_count);

  return Success;
}

