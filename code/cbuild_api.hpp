
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/arena.hpp"
#include "anyfin/list.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/prelude.hpp"
#include "anyfin/slice.hpp"
#include "anyfin/startup.hpp"
#include "anyfin/file_system.hpp"

#include "templates/cbuild_api_template.hpp"
#include "templates/cbuild_api_experimental_template.hpp"

#include "cbuild.hpp"

struct Target_Tracker;

enum Target_Arch {
  Target_Arch_x86,
  Target_Arch_x64
};

struct Target;

struct Arguments {
  Memory_Arena &global_arena;
  Slice<Startup_Argument> args;
};

struct User_Defined_Command {
  String      name;
  Action_Type proc;
};

struct Project {
  /*
    Perhaps it's not the cleanest approach it's worth to revisit it at a later point. This arena shouldn't used by any project
    configuration API, except when the project attempts to load a subproject, in which case we would use a global arena to allocate
    enough space for the subproject data, otherwise, the "arena" should be used.
   */
  Memory_Arena &global_arena;

  mutable Memory_Arena arena { make_sub_arena(global_arena, megabytes(2)) };

  /*
    Default configuration will be configured by the project's loader, that will lookup whatever is acceptible to
    build the project's configuration. User has capabilities to overwrite toolchain.
   */
  Toolchain_Configuration toolchain {};
  Target_Arch target_architecture = Target_Arch_x64;

  bool rebuild_required  = false;
  bool registry_disabled = false;

  List<User_Defined_Command> user_defined_commands { global_arena };

  /*
    If this is an external project, i.e it's loaded as a dependency by some other project, it will be given an
    "name". It'll be used by the root project as a prefix name/path for external targets to avoid
    potential collisions with other targets. The actual value is up to the user and can be set in the
    "register_external_project" call.
   */
  String name;
  bool   is_external;

  /*
    Top-level directory where the root of the project tree is. The folder will be used as a working directory
    with all the paths starting here. This is where the cache and project directories are.
   */
  File_Path project_root;

  /*
    Cache root is the path to the .cbuild directory where build residuals are persisted, along with the config's
    tag file and project's shared lib. It doesn't depend on the project's root path to correctly support external
    dependend projects, to avoid scenarios, for example, where a dependent project is in some form of the "extern"
    folder and we create new files there, which would be wrong.
   */
  File_Path cache_root;

  /*
    Path to the folder where all residual and final artifacts will be created (under corresponding nested folders).
    By default it's under '<root>/.cbuild/build', but the user is allowed to add more folders below that path. This
    could be helpful when multiple build configurations are introduced, e.g debug vs release build, etc... All other
    required folders, for object files and binaries, will be created in this folder.
   */
  File_Path build_location_path { make_file_path(arena, cache_root, "build") };

  File_Path install_location_path { make_file_path(arena, cache_root, "bin") };

  const File_Path project_output_location { make_file_path(arena, cache_root, "project") };
  const File_Path project_library_path    { make_file_path(arena, project_output_location, concat_string(arena, name, ".", get_shared_library_extension())) };

  List<Project *> sub_projects { arena };

  /*
    Various cbuild api calls return pointer to the targets contained in this list. While project owns all declared targets
    care must be taking in changing the container to something else, to avoid issue with potential reallocations.
    Just in case this would be replaced with a hash table some day.
   */
  List<Target> targets { arena };
  u32          total_files_count = 0;

  List<File_Path> include_paths { arena };

  List<String> compiler { arena };
  List<String> archiver { arena };
  List<String> linker   { arena };

  const Arguments *args = nullptr;

  struct {
  } hooks;

  Project (Memory_Arena &global, String _name,
           File_Path _project_root, File_Path _cache_directory,
           const bool _is_external = false)
    : global_arena { global },
      name         { copy_string(arena, _name) },
      is_external  { _is_external },
      project_root { copy_string(arena, _project_root) },
      cache_root   { make_file_path(arena, _cache_directory) }
  {}
};

struct Target {
  /*
    I'd love to get rid of this limitation, as it's merely an internal convenience and doesn't carry much
    value to the user, but with having a static size for the name would simplify registry's layout, alleviating
    the need to move the memory manually. It's still worth to reconsider this at a later pointer, perhaps there's
    a better way to do this, without inflicting any limitation on the user?
  */
  constexpr static usize Max_Name_Limit = 32;

  enum struct Type {
    Static_Library,
    Shared_Library,
    Executable,
  };

  using enum Type;

  String name;
  Type   type;

  /*
    IMPORANT: For the external targets this would point to the external project, not the main project
    that we are building now.
   */
  Project &project;

  struct {
    bool external = false;
    bool install  = false;
  } flags;

  List<File_Path> files          { project.arena };
  List<File_Path> include_paths  { project.arena };
  List<String>    link_libraries { project.arena };

  List<Target *> depends_on  { project.arena };
  List<Target *> required_by { project.arena };

  List<String> compiler { project.arena };
  List<String> archiver { project.arena };
  List<String> linker   { project.arena };

  struct {
    Hook_Func on_linked = nullptr;
  } hooks;

  /*
    These fields are set/accessed by the project builder.
   */
  struct {
    Target_Tracker *tracker = nullptr;

    /*
      These two field point to the Registry::Target_Info struct, but since I can't forward
      declare a nested struct, and I don't want to mess with headers, just defaulting to voids
      here. Otherwise it's safe to reinterpret_cast<Registry::Target_Info *>(...) in the builder.

      TODO: Move these to into target_tracker?
    */
    void *info      = nullptr;
    void *last_info = nullptr;
  } build_context;

  Target (Project &_project, Type _type, String _name)
    : name    { move(_name) },
      type    { _type },
      project { _project },
      flags   { .external = project.is_external }
  {}
};

String get_target_extension (const Target &target);
File_Path get_output_file_path_for_target (Memory_Arena &arena, const Target &target);
