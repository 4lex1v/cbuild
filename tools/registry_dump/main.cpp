
#include "anyfin/arena.hpp"
#include "anyfin/console.hpp"

#include "code/registry.hpp"

Panic_Handler panic_handler = terminate;

int mainCRTStartup () {
  Memory_Arena arena { reserve_virtual_memory(megabytes(1)) };

  auto args = get_startup_args(arena);

  File_Path registry_file_path;
  if (args.count > 1) registry_file_path = make_file_path(arena, args[1].key).value;
  else registry_file_path = make_file_path(arena, ".cbuild", "build", "debug", "win32", "__registry").value;

  if (check_file_exists(registry_file_path).value == false) {
    write_to_stdout("Registry file not found, please check that the path is correct and that the file actually exists\n");
    return 1;
  }

  Registry registry = load_registry(arena, registry_file_path);

  auto &records = registry.records;
  auto &header  = records.header;

  auto targets_count = header.targets_count;
  auto total_files_count = 0;
  for (usize idx = 0; idx < targets_count; idx++) total_files_count += records.targets[idx].files_count.value;

  write_to_stdout(format_string(arena, "Version: %\n",  header.version));
  write_to_stdout(format_string(arena, "Targets: #%\n", targets_count));
  write_to_stdout(format_string(arena, "Files:   #% (#%)\n", total_files_count, header.aligned_total_files_count));
  write_to_stdout(format_string(arena, "Dependencies: %\n", header.dependencies_count));

  write_to_stdout("\nTarget Info: \n");
  for (usize idx = 0; idx < targets_count; idx++) {
    auto target = records.targets + idx;

    write_to_stdout(format_string(arena, "  Name: %\n", reinterpret_cast<const char *>(target->name)));
    write_to_stdout(format_string(arena, "    - Offset: %\n", target->files_offset));
    write_to_stdout(format_string(arena, "    - Files: #%\n", target->files_count.value));
    write_to_stdout(format_string(arena, "    - Aligned: #%\n", target->aligned_max_files_count));
    write_to_stdout(format_string(arena, "\n"));
  }

  write_to_stdout("\nFiles:\n");
  for (usize idx = 0; idx < header.aligned_total_files_count; idx++) {
    write_to_stdout(format_string(arena, "  %) ID: %, TS: %, H: %\n", idx, records.files[idx], records.file_records[idx].timestamp, records.file_records[idx].hash));
  }

  write_to_stdout("\nDependencies:\n");
  for (usize idx = 0; idx < header.dependencies_count; idx++) {
    write_to_stdout(format_string(arena, "  %) ID: %, TS: %, H: %\n", idx, records.dependencies[idx], records.dependency_records[idx].timestamp, records.dependency_records[idx].hash));
  }
  
  return 0;
}
