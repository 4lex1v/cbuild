
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/atomics.hpp"

#include "cbuild_api.hpp"

/*
  Capping the number of files the tool supports per project, largely for the purposes of
  registry update set allocation. At this point I'm not targetting large projects support,
  at this point Linux Kernel has 63k files, so this limit should be more than enough, if
  anything it could be changed later.
 */
static usize max_supported_files_count = 250'000;

struct Registry {
  constexpr static usize Version = 1;

  struct Header {
    u16 version;
    u16 targets_count;
    u32 aligned_total_files_count;
    u32 dependencies_count;

    u32 _reserved[61];
  };

  static_assert(sizeof(Header) == sizeof(u64) * 32);

  struct Record {
    u64 timestamp;
    u64 hash;
  };

  struct Target_Info {
    char name[Target::Max_Name_Limit];

    u64  files_offset;
    au64 files_count;
    u32  aligned_max_files_count;
  };

  File  registry_file;
  File_Mapping registry_file_mapping;

  struct {
    Header header;

    Target_Info *targets;

    u64    *files;
    Record *file_records;

    u64    *dependencies;
    Record *dependency_records;
  } records;
};

static Array<u64> get_dependencies (Registry &registry) {
  return Array(registry.records.dependencies, registry.records.header.dependencies_count);
}

struct Update_Set {
  u8 *buffer;

  Registry::Header *header;

  Registry::Target_Info *targets;

  u64 *files;
  Registry::Record *file_records;

  u64 *dependencies;
  Registry::Record *dependency_records;
};

static Array<u64> get_dependencies (Update_Set &set) {
  return Array(set.dependencies, set.header->dependencies_count);
}

Registry load_registry (Memory_Arena &arena, File_Path registry_file_path);

Update_Set init_update_set (Memory_Arena &arena, const Registry &registry, const Project &project);

void flush_registry (Registry &registry, const Update_Set &update_set);
