
#pragma once

#include "base.hpp"
#include "cbuild_api.hpp"
#include "atomics.hpp"

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

  File registry_file;

  struct {
    File_Mapping registry_file_mapping;

    Header header;

    Target_Info *targets;

    u64    *files;
    Record *file_records;

    u64    *dependencies;
    Record *dependency_records;
  } records;
};

Status_Code load_registry (Registry *registry, Memory_Arena *arena, const File_Path *registry_file_path);
