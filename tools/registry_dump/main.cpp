
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>

#include "code/registry.hpp"
#include "code/platform.hpp"
#include "code/runtime.hpp"

int main (int argc, char **argv) {
  enum { buffer_size = megabytes(1) };
  auto buffer = new char[buffer_size];
  defer { delete[] buffer; };

  auto arena = Memory_Arena { buffer, buffer_size };

  File_Path registry_file_path;
  if (argc > 1) registry_file_path = make_file_path(&arena, argv[1]).value;
  else registry_file_path = make_file_path(&arena, ".cbuild", "build", "debug", "win32", "__registry").value;

  if (!check_file_exists(&registry_file_path)) {
    print(&arena, "Registry file not found, please check that the path is correct and that the file actually exists\n");
    return 1;
  }

  auto registry_file = open_file(&registry_file_path);
  check_status(registry_file);

  Registry registry;
  auto load_status = load_registry(&registry, &arena, registry_file);
  if (!load_status) {
    printf("Couldn't load registry data from file, something fishy going on here\n");
    return 1;
  }

  auto &records = registry.records;
  auto &header  = records.header;

  auto targets_count = header.targets_count;
  auto total_files_count = 0;
  for (usize idx = 0; idx < targets_count; idx++) total_files_count += records.targets[idx].files_count.value;

  print(&arena, "Version: %\n",  header.version);
  print(&arena, "Targets: #%\n", targets_count);
  print(&arena, "Files:   #% (#%)\n", total_files_count, header.aligned_total_files_count);
  print(&arena, "Dependencies: %\n", header.dependencies_count);

  print(&arena, "\nTarget Info: \n");
  for (usize idx = 0; idx < targets_count; idx++) {
    auto target = records.targets + idx;

    print(&arena, "  Name: %\n", target->name);
    print(&arena, "    - Offset: %\n", target->files_offset);
    print(&arena, "    - Files: #%\n", target->files_count.value);
    print(&arena, "    - Aligned: #%\n", target->aligned_max_files_count);
    print(&arena, "\n");
  }

  print(&arena, "\nFiles:\n");
  for (usize idx = 0; idx < header.aligned_total_files_count; idx++) {
    print(&arena, "  %) ID: %, TS: %, H: %\n", idx, records.files[idx], records.file_records[idx].timestamp, records.file_records[idx].hash);
  }

  print(&arena, "\nDependencies:\n");
  for (usize idx = 0; idx < header.dependencies_count; idx++) {
    print(&arena, "  %) ID: %, TS: %, H: %\n",
           idx,
           records.dependencies[idx],
           records.dependency_records[idx].timestamp, records.dependency_records[idx].hash);
  }
  
  return 0;
}
