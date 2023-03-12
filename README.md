# CBuild - build tool for C/C++ projects.

CBuild is a handmade build tool for C/C++ projects, designed for modern computing systems. Unlike other build tools
available today, this tool takes the approach of programs like Make and Ninja - you define a project's configuration and
build right away. In addition, CBuild offers useful features such as the ability to create user-defined commands in
C/C++ and execute them from the command line. It also provides the flexibility to define user-specific hooks for various
stages of the project's build process.

## Building project - is just another program.

Over the years, I have encountered numerous challenges when working with other build systems and tools. Here are a few of those stumbling blocks:
- Maintaining these configurations at scale can get ridiculous because of poor choices in DSL design.
- Need to tell the build tool about header files.
- In some build systems adding custom logic for your personal needs is challenging if possible at all.
- No way to debug the configuration.

For a long time I've been using shell scripts to build projects, it's easy to get started, you have full control over
what exactly is passed to the compiler, and writing custom logic is a no-brainer. Unfortunately, with scale, this approach
becomes inconvenient and limiting, not that you can't write more scripts and build such infrastructure, but at that point,
it would just mimic what Make has been doing for all these years.

I wanted a better tool, a tool where I could configure projects using a language I'm comfortable with, offering a
straightforward API, and providing the ability to write custom logic with ease. More importantly, it should grant full
control over the build process and include a debugging feature to troubleshoot any issues promptly. At the core, my 
desire was to build projects using another program written in C, which led to the inception of CBuild.

## System Requirements

x64 CPU with AVX2 support.

## Getting Started

- Download the binary
- Create a folder for the project and run `cbuild init`
  - This should create base files for the project and a main file to get you started
- Call `cbuild build` to build the project

## Example

Please check out [CBuild's](./project/build.cpp) configuration for a fuller example.

Otherwise, here's a minimal example to build this project:

```c
#include "cbuild.h"

bool setup_project (const Arguments *args, Project *project) {
  Target *cbuild = add_executable(project, "cbuild");
  add_all_sources_from_directory(cbuild, "code", "c", false);
  return true;
}
```