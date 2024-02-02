
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include <string_view>
#include <filesystem>
#include <string>
#include <fstream>

#include "cbuild.h"
#include "cbuild_experimental.h"

using u32 = uint32_t;

/*
  Custom Commands
 */
static int generate_headers (const Arguments *args) noexcept;

// static void print_hashes (const Project *project, const Target *target, const Arguments *args, Hook_Type type) {
//   if (!strstr(get_argument_or_default(args, "config", "debug"), "release")) return;

//   // For this I'd need to get a target and get its generated files
//   auto file_path = get_generated_binary_file_path(target);

//   char command[2046];
//   { // Print MD5
//     snprintf(command, 2046, "certutil -hashfile %s MD5", file_path);
//     std::system(command);
//   }

//   { // Print SHA256
//     snprintf(command, 2046, "certutil -hashfile %s SHA256", file_path);
//     std::system(command);
//   }

//   { // generate gpg signature
//     snprintf(command, 2046, "gpg --detach-sign -o cbuild.sig %s", file_path);
//     std::system(command);
//   }
// }

static bool read_versions (u32 *tool, u32 *api) {
  auto file = fopen("./versions", "rb");
  if (file == NULL) {
    printf("Failed to open versions file\n");
    return false;
  }

  if (fscanf(file, "%u", tool) != 1) {
    printf("Failed to read tool's version\n");
    fclose(file);
    return false;
  }

  if (fscanf(file, "%u", api) != 1) {
    printf("Failed to read api's version\n");
    fclose(file);
    return false;
  }

  fclose(file);
  return true;
}

extern "C" bool setup_project (const Arguments *args, Project *project) {
  std::string_view config   = get_argument_or_default(args, "config",   "debug");
  std::string_view platform = get_argument_or_default(args, "platform", "win32");

  const bool debug_build = config == "debug";

  register_action(project, "generate", generate_headers);

  set_toolchain(project, Toolchain_Type_LLVM);

  char output_location[256];
  snprintf(output_location, 256, "%s/%s", config.data(), platform.data());
  set_output_location(project, output_location);


  u32 tool_version = 0, api_version = 0;
  if (!read_versions(&tool_version, &api_version)) return false;
  char versions[256];
  sprintf(versions, "-DTOOL_VERSION=%u -DAPI_VERSION=%u", tool_version, api_version);

  add_global_include_search_paths(project, ".", "libs/anyfin");
  add_global_compiler_options(project, "-std=c++2b",
                              versions,
                              "-DCPU_ARCH_X64 -DPLATFORM_WIN32 -DPLATFORM_WIN32",
                              "-march=x86-64 -mavx2 -masm=intel -fdiagnostics-absolute-paths",
                              "-nostdlib -nostdlib++ -nostdinc++");

  add_global_compiler_option(project, debug_build ? "-O0 -DDEV_BUILD -g -gcodeview" : "-O3");

  if (debug_build) add_global_linker_option(project, "/debug:full");
  add_global_linker_options(project, "/nologo /subsystem:console");

  auto cbuild = add_executable(project, "cbuild");
  {
    add_source_file(cbuild, "code/builder.cpp");
    add_source_file(cbuild, "code/cbuild.cpp");
    add_source_file(cbuild, "code/cbuild_api.cpp");
    add_source_file(cbuild, "code/logger.cpp");
    add_source_file(cbuild, "code/registry.cpp");
    add_source_file(cbuild, "code/scanner.cpp");
    add_source_file(cbuild, "code/toolchain_win32.cpp");
    add_source_file(cbuild, "code/workspace.cpp");

    add_compiler_options(cbuild, "-fno-exceptions");

    if (platform == "win32") {
      char exports_option[256] = "/def:";
      snprintf(exports_option + 5, 256-5, "%s\\cbuild.def", std::filesystem::current_path().string().c_str());
      add_linker_option(cbuild, exports_option);
    }

    link_with(cbuild, "kernel32.lib", "advapi32.lib", "shell32.lib", "winmm.lib");
  }

  auto tests = add_executable(project, "tests");
  {
    add_all_sources_from_directory(tests, "tests", "cpp", false);
    add_source_files(tests, "code/cbuild_api.cpp", "code/toolchain_win32.cpp", "code/logger.cpp");

    add_compiler_option(tests, "-DCBUILD_ENABLE_EXCEPTIONS");

    link_with(tests, "kernel32.lib", "advapi32.lib", "shell32.lib", "libcmt.lib");
  }

  auto rdump = add_executable(project, "rdump");
  {
    add_all_sources_from_directory(rdump, "tools/registry_dump", "cpp", false);
    add_source_files(rdump, "code/registry.cpp", "code/logger.cpp");
    add_compiler_options(cbuild, "-fno-exceptions");
    link_with(rdump, "kernel32.lib", "advapi32.lib");
  }

  // if (config == "release") {
  //   char release_folder[128];
  //   snprintf(release_folder, 128, "releases/r%u/%s", tool_version, platform.data());
  //   set_output_location(project, release_folder);
  // }

  //add_target_hook(cbuild, Hook_Type_After_Target_Linked, print_hashes);
  
  return true;
}

static int generate_headers (const Arguments *args) noexcept {
  std::string_view platform = get_argument_or_default(args, "platform", "win32");

  u32 tool_version = 0, api_version = 0;
  if (!read_versions(&tool_version, &api_version)) {
    return EXIT_FAILURE;
  }

  char api_version_str[16];
  auto api_version_str_length = sprintf(api_version_str, "%u", api_version);

  printf("Generating header for version: %u\n", api_version);

  auto output_file_path = "./code/templates/generated.h";
  auto output_file      = fopen(output_file_path, "wb+");
  if (output_file == nullptr) {
    printf("FATAL ERROR: Couldn't open a file handle for %s\n", output_file_path);
    return EXIT_FAILURE;
  }

  {
    auto api_template_file_path = "./code/templates/cbuild_api_template.hpp";
    auto api_template_content   = fopen(api_template_file_path, "rb+");
    if (api_template_content == nullptr) {
      printf("FATAL ERROR: Failed to find the header file at: %s\n", api_template_file_path);
      fclose(output_file);
      return EXIT_FAILURE;
    }

    auto search        = "CBUILD_API_VERSION";
    auto search_length = strlen(search);
    
    fseek(api_template_content, 0, SEEK_END);
    long file_size = ftell(api_template_content);
    fseek(api_template_content, 0, SEEK_SET);

    char *buffer = reinterpret_cast<char *>(malloc(file_size + 1));
    // Read the file content into the buffer
    size_t read_size = fread(buffer, sizeof(char), file_size, api_template_content);
    buffer[read_size] = '\0'; // Null-terminate the buffer

    // Write the version into the header
    auto placeholder = strstr(buffer, search);
    assert(placeholder);

    memcpy(placeholder, api_version_str, api_version_str_length);

    auto offset = placeholder - buffer;
    memmove(placeholder + api_version_str_length, placeholder + search_length, file_size - (offset + search_length));
    file_size -= (search_length - api_version_str_length);
    buffer[file_size] = '\0';

    auto header_start = "\nstatic const unsigned char cbuild_api_content[] = { ";
    auto header_end   = "};\n\n";

    fwrite(header_start, strlen(header_start), 1, output_file);

    // Iterate through the buffer and write the hexadecimal bytes
    for (long i = 0; i < file_size; i++) {
      fprintf(output_file, "0x%02x, ", (unsigned char)buffer[i]);
    }

    fwrite(header_end, strlen(header_end), 1, output_file);

    fprintf(output_file, "static const unsigned int cbuild_api_content_size = %li;\n", file_size);
    fprintf(output_file, "static_assert(cbuild_api_content_size > 0);\n");
    fprintf(output_file, "static_assert(cbuild_api_content_size == (sizeof(cbuild_api_content) / sizeof(cbuild_api_content[0])));\n");

    fflush(output_file);

    free(buffer);
    fclose(api_template_content);
  }

  { // experimental api
    auto file_path = "./code/templates/cbuild_api_experimental_template.hpp";
    auto content   = fopen(file_path, "rb+");
    if (content == nullptr) {
      printf("FATAL ERROR: Failed to find the header file at: %s\n", file_path);
      fclose(output_file);
      return EXIT_FAILURE;
    }

    fseek(content, 0, SEEK_END);
    const long file_size = ftell(content);
    fseek(content, 0, SEEK_SET);

    char *buffer = reinterpret_cast<char *>(malloc(file_size + 1));
    // Read the file content into the buffer
    size_t read_size = fread(buffer, sizeof(char), file_size, content);
    buffer[read_size] = '\0'; // Null-terminate the buffer

    auto header_start = "\nstatic const unsigned char cbuild_experimental_api_content[] = { ";
    auto header_end   = "};\n\n";

    fwrite(header_start, strlen(header_start), 1, output_file);

    // Iterate through the buffer and write the hexadecimal bytes
    for (long i = 0; i < file_size; i++) {
      fprintf(output_file, "0x%02x, ", (unsigned char)buffer[i]);
    }

    fwrite(header_end, strlen(header_end), 1, output_file);

    fprintf(output_file, "static const unsigned int cbuild_experimental_api_content_size = %li;\n", file_size);
    fprintf(output_file, "static_assert(cbuild_experimental_api_content_size > 0);\n");
    fprintf(output_file, "static_assert(cbuild_experimental_api_content_size == (sizeof(cbuild_experimental_api_content) / sizeof(cbuild_experimental_api_content[0])));\n");

    fflush(output_file);

    free(buffer);
    fclose(content);
  }

  { // build_template
    auto build_template_file_path = "./code/templates/build_template.hpp";
    auto build_template_content   = fopen(build_template_file_path, "rb+");
    if (build_template_content == nullptr) {
      printf("FATAL ERROR: Failed to find the header file at: %s\n", build_template_file_path);
      fclose(output_file);
      return EXIT_FAILURE;
    }

    fseek(build_template_content, 0, SEEK_END);
    const long file_size = ftell(build_template_content);
    fseek(build_template_content, 0, SEEK_SET);

    char *buffer = reinterpret_cast<char *>(malloc(file_size + 1));
    // Read the file content into the buffer
    size_t read_size = fread(buffer, sizeof(char), file_size, build_template_content);
    buffer[read_size] = '\0'; // Null-terminate the buffer

    auto header_start = "\nstatic const unsigned char build_template_content[] = { ";
    auto header_end   = "};\n\n";

    fwrite(header_start, strlen(header_start), 1, output_file);

    // Iterate through the buffer and write the hexadecimal bytes
    for (long i = 0; i < file_size; i++) {
      fprintf(output_file, "0x%02x, ", (unsigned char)buffer[i]);
    }

    fwrite(header_end, strlen(header_end), 1, output_file);

    fprintf(output_file, "static const unsigned int build_template_content_size = %li;\n", file_size);
    fprintf(output_file, "static_assert(build_template_content_size > 0);\n");
    fprintf(output_file, "static_assert(build_template_content_size == (sizeof(build_template_content) / sizeof(build_template_content[0])));\n");

    fflush(output_file);

    free(buffer);
    fclose(build_template_content);
  }

  { // main_cpp_template
    auto main_cpp_file_path = "./code/templates/main_cpp_template.hpp";
    auto main_cpp_content   = fopen(main_cpp_file_path, "rb+");
    if (main_cpp_content == nullptr) {
      printf("FATAL ERROR: Failed to find the header file at: %s\n", main_cpp_file_path);
      fclose(output_file);
      return EXIT_FAILURE;
    }

    fseek(main_cpp_content, 0, SEEK_END);
    const long file_size = ftell(main_cpp_content);
    fseek(main_cpp_content, 0, SEEK_SET);

    char *buffer = reinterpret_cast<char *>(malloc(file_size + 1));
    // Read the file content into the buffer
    size_t read_size = fread(buffer, sizeof(char), file_size, main_cpp_content);
    buffer[read_size] = '\0'; // Null-terminate the buffer

    auto header_start = "\nstatic const unsigned char main_cpp_content[] = { ";
    auto header_end   = "};\n\n";

    fwrite(header_start, strlen(header_start), 1, output_file);

    // Iterate through the buffer and write the hexadecimal bytes
    for (long i = 0; i < file_size; i++) {
      fprintf(output_file, "0x%02x, ", (unsigned char)buffer[i]);
    }

    fwrite(header_end, strlen(header_end), 1, output_file);

    fprintf(output_file, "static const unsigned int main_cpp_content_size = %li;\n", file_size);
    fprintf(output_file, "static_assert(main_cpp_content_size > 0);\n");
    fprintf(output_file, "static_assert(main_cpp_content_size == (sizeof(main_cpp_content) / sizeof(main_cpp_content[0])));\n");

    fflush(output_file);

    free(buffer);
    fclose(main_cpp_content);
  }

  if (platform == "win32") {
    auto cbuild_def_file_path = "cbuild.def";
    auto cbuild_def_content   = fopen(cbuild_def_file_path, "rb+");
    if (cbuild_def_content == nullptr) {
      printf("FATAL ERROR: File not found: %s\n", cbuild_def_file_path);
      fclose(output_file);
      return EXIT_FAILURE;
    }

    fseek(cbuild_def_content, 0, SEEK_END);
    const long file_size = ftell(cbuild_def_content);
    fseek(cbuild_def_content, 0, SEEK_SET);

    char *buffer = reinterpret_cast<char *>(malloc(file_size + 1));
    // Read the file content into the buffer
    size_t read_size = fread(buffer, sizeof(char), file_size, cbuild_def_content);
    buffer[read_size] = '\0'; // Null-terminate the buffer

    auto header_start = "\n#ifdef PLATFORM_WIN32\nstatic const unsigned char cbuild_def_content[] = { ";
    auto header_end   = "};\n\n";

    fwrite(header_start, strlen(header_start), 1, output_file);

    // Iterate through the buffer and write the hexadecimal bytes
    for (long i = 0; i < file_size; i++) {
      fprintf(output_file, "0x%02x, ", (unsigned char)buffer[i]);
    }

    fwrite(header_end, strlen(header_end), 1, output_file);

    fprintf(output_file, "static const unsigned int cbuild_def_content_size = %li;\n", file_size);
    fprintf(output_file, "static_assert(cbuild_def_content_size > 0);\n");
    fprintf(output_file, "static_assert(cbuild_def_content_size == (sizeof(cbuild_def_content) / sizeof(cbuild_def_content[0])));\n");
    fprintf(output_file, "#endif\n");

    fflush(output_file);

    free(buffer);
    fclose(cbuild_def_content);
  }

  fclose(output_file);

  return EXIT_SUCCESS;
}


