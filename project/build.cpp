
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

static std::string_view config;
static std::string_view platform;

/*
  Custom Commands
 */
static int generate_headers (const Arguments *args);
static int generate_tags    (const Arguments *args);

static void install_hook (const Project *project, const Target *target, const Arguments *args, Hook_Type type) {
//  printf("Installing target: %s!\n", get_target_name(target));
}

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
  config    = get_argument_or_default(args, "config",   "debug");
  platform  = get_argument_or_default(args, "platform", "win32");

  char output_location[256];
  snprintf(output_location, 256, "%s/%s", config.data(), platform.data());
  set_output_location(project, output_location);

  set_toolchain(project, Toolchain_Type_LLVM);

  disable_registry(project);
  register_action(project, "generate", generate_headers);
  register_action(project, "tags",     generate_tags);

  bool is_debug = config == "debug";

  u32 tool_version = 0, api_version = 0;
  if (!read_versions(&tool_version, &api_version)) return false;
  char versions[256];
  sprintf(versions, "-DTOOL_VERSION=%u -DAPI_VERSION=%u", tool_version, api_version);

  auto configure = [&] (Target *target) {
    add_all_sources_from_directory(target, "./code", "cpp", false);
    add_compiler_options(target, "-std=c++20 -DPLATFORM_X64");
    add_compiler_options(target, versions);

    add_compiler_options(target,
                         (is_debug)                        ? "-O0 -g -DDEV_BUILD" : "-O3",
                         (is_debug && platform == "win32") ? "-gcodeview"           : "",
                         "-march=x86-64 -mavx2 -masm=intel -fno-exceptions -fdiagnostics-absolute-paths");
  
    if (platform == "win32") {
      add_compiler_options(target, "-DPLATFORM_WIN32");

      char exports_option[256] = "/def:";
      snprintf(exports_option + 5, 256-5, "%s\\cbuild.def", std::filesystem::current_path().string().c_str());
      add_linker_options(target, exports_option);

      if (config == "debug") add_linker_options(target, "/debug:full");
      add_linker_options(target, "/subsystem:console");

      link_with(target, "kernel32.lib", "libcmt.lib", "Advapi32.lib", "shell32.lib");
    }
  };

  auto cbuild = add_executable(project, "cbuild");
  configure(cbuild);

  if (config == "release") {
    char release_folder[128];
    snprintf(release_folder, 128, "releases/r%u/%s", tool_version, platform.data());

    set_output_location(project, release_folder);
  }
  
  return true;
}

static int generate_headers (const Arguments *args) {
  u32 tool_version = 0, api_version = 0;
  if (!read_versions(&tool_version, &api_version)) {
    return EXIT_FAILURE;
  }

  char api_version_str[16];
  auto api_version_str_length = sprintf(api_version_str, "%u", api_version);

  printf("Generating header for version: %u\n", api_version);

  auto output_file_path = "./code/generated.h";
  auto output_file      = fopen(output_file_path, "wb+");
  if (output_file == nullptr) {
    printf("FATAL ERROR: Couldn't open a file handle for %s\n", output_file_path);
    return EXIT_FAILURE;
  }

  {
    auto api_template_file_path = "./code/cbuild_api_template";
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

  {
    // experimental api
    auto file_path = "./code/cbuild_api_experimental";
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

  {
    // build_template
    auto build_template_file_path = "./code/build_template";
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

    {
    // main_cpp_template
    auto main_cpp_file_path = "./code/main_cpp_template";
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

#ifdef _WIN32
  {
    if (std::filesystem::create_directory(".cbuild/tmp") &&
        system("lib.exe /nologo /def:cbuild.def /out:.cbuild\\tmp\\cbuild.lib /machine:x64")) {
      printf("FATAL ERROR: cbuild import lib generation failed\n");
      return EXIT_FAILURE;
    }

    // cbuild.lib
    auto cbuild_lib_file_path = ".\\.cbuild\\tmp\\cbuild.lib";
    auto cbuild_lib_content   = fopen(cbuild_lib_file_path, "rb+");
    if (cbuild_lib_content == nullptr) {
      printf("FATAL ERROR: File not found: %s\n", cbuild_lib_file_path);
      fclose(output_file);
      return EXIT_FAILURE;
    }

    fseek(cbuild_lib_content, 0, SEEK_END);
    const long file_size = ftell(cbuild_lib_content);
    fseek(cbuild_lib_content, 0, SEEK_SET);

    char *buffer = reinterpret_cast<char *>(malloc(file_size + 1));
    // Read the file content into the buffer
    size_t read_size = fread(buffer, sizeof(char), file_size, cbuild_lib_content);
    buffer[read_size] = '\0'; // Null-terminate the buffer

    auto header_start = "\n#ifdef PLATFORM_WIN32\nstatic const unsigned char cbuild_lib_content[] = { ";
    auto header_end   = "};\n\n";

    fwrite(header_start, strlen(header_start), 1, output_file);

    // Iterate through the buffer and write the hexadecimal bytes
    for (long i = 0; i < file_size; i++) {
      fprintf(output_file, "0x%02x, ", (unsigned char)buffer[i]);
    }

    fwrite(header_end, strlen(header_end), 1, output_file);

    fprintf(output_file, "static const unsigned int cbuild_lib_content_size = %li;\n", file_size);
    fprintf(output_file, "static_assert(cbuild_lib_content_size > 0);\n");
    fprintf(output_file, "static_assert(cbuild_lib_content_size == (sizeof(cbuild_lib_content) / sizeof(cbuild_lib_content[0])));\n");
    fprintf(output_file, "#endif\n");

    fflush(output_file);

    free(buffer);
    fclose(cbuild_lib_content);
  }
#endif

  fclose(output_file);

  return EXIT_SUCCESS;
}

static int generate_tags (const Arguments *args) {
  printf("Generating TAGS file for Emacs\n");

  std::filesystem::remove("TAGS");

  auto build_tags = [&](const std::filesystem::path &folder, const std::string &extension) {
    for (const auto &entry: std::filesystem::directory_iterator(folder)) {
      if (entry.path().extension() == "." + extension) {
        std::system(("etags -a " + entry.path().string()).c_str());
      }
    }
  };

  build_tags("./code", "hpp");
  build_tags("./code", "cpp");

  return EXIT_SUCCESS;
}

