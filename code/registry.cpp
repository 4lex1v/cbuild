
#include "cbuild_api.hpp"
#include "registry.hpp"
#include "runtime.hpp"

Status_Code load_registry (Registry *registry, Memory_Arena *arena, File registry_file) {
  using enum Open_File_Flags;
  use(Status_Code);

  auto &records = registry->records;

  registry->registry_file = registry_file;

  auto file_size = get_file_size(&registry_file);
  if (file_size == 0) return Success;

  auto mapping = map_file_into_memory(&registry_file);
  check_status(mapping);

  registry->registry_file_mapping = mapping;

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

Status_Code init_update_set (Update_Set *update_set, Memory_Arena *arena, Registry *registry, const Project *project) {
  auto &records = registry->records;
  
  auto aligned_files_count = records.header.aligned_total_files_count;
  {
    u16 new_aligned_total = 0;

    /*
      It's aligned by 4 to put the size of the allocated buffer on a 32-byte boundary, each file record is 8 bytes.
    */
    for (auto target: project->targets) new_aligned_total += align_forward(target->files.count, 4);

    /*
      If the number of files in the project reduced, copying old info for the new allocation will cause memory
      corruption (for trying to copy more data than we have allocated memory for), thus we need to allocate enough
      space to hold everything, we'll write the correct number of files into the file anyway.
    */
    if (new_aligned_total > aligned_files_count) aligned_files_count = new_aligned_total;
  }

  assert(is_aligned_by(aligned_files_count, 4));

  if (aligned_files_count > max_supported_files_count) {
    return { Status_Code::Invalid_Value, "At the moment cbuild is limited to support 250k files." };
  }
  auto dependencies_limit = max_supported_files_count - aligned_files_count;

  auto update_set_buffer = get_memory_at_current_offset<u8>(arena, 32);
  auto buffer_cursor     = update_set_buffer;

  auto set_field = [&buffer_cursor] <typename T> (T *field, usize count, usize align = 0) {
    if (align > 0) buffer_cursor = align_forward(buffer_cursor, align);

    auto value_size = sizeof(remove_ptr_t<T>) * count;

    *field         = reinterpret_cast<T>(buffer_cursor);
    buffer_cursor += value_size;
  };

  set_field(&update_set->header,             1);
  set_field(&update_set->targets,            project->targets.count);
  set_field(&update_set->files,              aligned_files_count, 32);
  set_field(&update_set->file_records,       aligned_files_count);
  set_field(&update_set->dependencies,       dependencies_limit, 32);
  set_field(&update_set->dependency_records, dependencies_limit);

  auto reservation_size = buffer_cursor - update_set_buffer;
  {
    auto reservation = reserve_memory(arena, reservation_size, 32);
    if (reservation == nullptr) return { Status_Code::Out_Of_Memory, "Not enough memory to allocate buffer for registry update set" };

    assert((void*)reservation == (void*)update_set_buffer);
    assert((void*)(update_set_buffer + reservation_size) == (void*)get_memory_at_current_offset(arena));
  }

  zero_memory(update_set_buffer, reservation_size);

  update_set->buffer = update_set_buffer;

  *update_set->header = Registry::Header {
    .version                   = Registry::Version,
    .targets_count             = static_cast<u16>(project->targets.count),
    .aligned_total_files_count = aligned_files_count,
    .dependencies_count        = 0,
  };

  /*
    We needs to copy information from the old registry for the existing targets to update that information later,
    as well as drop obsolete targets that were removed from the registry.
   */
  for (usize target_index = 0, files_offset = 0; auto target: project->targets) {
    auto info = update_set->targets + target_index;

    target->info = info;
    copy_memory(info->name, target->name.value, target->name.length);

    for (usize idx = 0; idx < records.header.targets_count; idx++) {
      auto old_info = records.targets + idx;

      if (strncmp(info->name, old_info->name, Target::Max_Name_Limit) == 0) {
        target->last_info = old_info;
        break;
      }
    }

    // The boundary of each segment for target files should still be aligned on 32-bytes.
    info->aligned_max_files_count = align_forward(target->files.count, 4);
    info->files_offset            = files_offset;

    target_index += 1;
    files_offset += info->aligned_max_files_count;
  }

  return Status_Code::Success;
}

Status_Code flush_registry (Registry *registry, const Update_Set *update_set) {
  use(Status_Code);

  reset_file_cursor(&registry->registry_file);

  auto count = update_set->header->dependencies_count;
  auto records = copy_memory(reinterpret_cast<Registry::Record *>(update_set->dependencies + count), update_set->dependency_records, count);

  auto flush_buffer_size = usize(records + count) - usize(update_set->buffer);

  write_buffer_to_file(&registry->registry_file, (char*)update_set->buffer, flush_buffer_size);

  close_file(&registry->registry_file);

  return Success;
}
