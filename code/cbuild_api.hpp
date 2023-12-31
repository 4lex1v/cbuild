
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/core/arena.hpp"
#include "anyfin/core/list.hpp"
#include "anyfin/core/strings.hpp"
#include "anyfin/core/prelude.hpp"

#include "anyfin/platform/files.hpp"

#include "cbuild_api_template.hpp"
#include "cbuild_api_experimental.hpp"

#include "cbuild.hpp"

using namespace Fin::Core;
using namespace Fin::Platform;

struct Target_Tracker;

enum Target_Arch {
  Target_Arch_x86,
  Target_Arch_x64
};

struct Target;

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
  Memory_Arena global_arena;

  mutable Memory_Arena arena;

  Target_Arch target_architecture = Target_Arch_x64;
  Toolchain_Configuration toolchain;

  bool rebuild_required  = false;
  bool registry_disabled = false;

  List<User_Defined_Command> user_defined_commands;

  /*
    If this is an external project, i.e it's loaded as a dependency by some other project, it will be given an
    "external_name". It'll be used by the root project as a prefix name/path for external targets to avoid
    potential collisions with other targets. The actual value is up to the user and can be set in the
    "register_external_project" call.
   */
  String external_name;
  bool   is_external;

  File_Path project_root;

  /*
    Path to the folder where all residual and final artifacts will be created (under corresponding nested folders).
    By default it's under '<root>/.cbuild/build', but the user is allowed to add more folders below that path. This
    could be helpful when multiple build configurations are introduced, e.g debug vs release build, etc... All other
    required folders, for object files and binaries, will be created in this folder.
   */
  File_Path output_location_path;

  List<Project *> sub_projects;

  List<Target *> targets;
  u32            total_files_count;

  struct {
    List<File_Path> include_paths;

    List<String> compiler;
    List<String> archiver;
    List<String> linker;
  } project_options;

  const Arguments *args = nullptr;

  struct {
    
  } hooks;

  Project (Memory_Arena &global,
           const String_View &name,
           File_Path root,
           File_Path _output_location,
           const bool _is_external = false)
    // Toolchain_Configuration default_toolchain)
    : global_arena          { global },
      arena                 { make_sub_arena(global, megabytes(2)) },
      //toolchain           { default_toolchain },
      user_defined_commands { global },
      external_name         { move(String::copy(global, name)) },
      is_external           { _is_external },
      project_root          { move(root) },
      output_location_path  { move(_output_location) },
      sub_projects          { arena },
      targets               { arena },
      project_options       { .include_paths { arena }, .compiler { arena }, .archiver { arena }, .linker { arena } }
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
  Project *project;

  struct {
    bool external = false;
  } flags;

  List<File_Path> files;
  List<File_Path> include_paths;
  List<String>    link_libraries;

  List<Target *> depends_on;
  List<Target *> required_by;

  struct {
    List<String> compiler;
    List<String> archiver;
    List<String> linker;
  } options;

  //File_Path output_file_path;

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
    : name           { move(_name) },
      type           { _type },
      project        { &_project },
      flags          { .external = project->is_external },
      files          { project->arena },
      include_paths  { project->arena },
      link_libraries { project->arena },
      depends_on     { project->arena },
      required_by    { project->arena },
      options {
        .compiler { project->arena },
        .archiver { project->arena },
        .linker   { project->arena }
      }
    {}
};

const char * get_argument_or_default (const Arguments *args, const char *key, const char *default_value);

const String_View & get_target_extension (const Target &target);
File_Path get_output_file_path_for_target (Memory_Arena &arena, const Target &target);
