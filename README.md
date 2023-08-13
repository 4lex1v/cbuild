# CBuild - build tool for C/C++ programmers.

CBuild is a small and efficient build tool for C/C++ programmers, designed to take full advantage of modern hardware. It offers a native API for writing a meta-program—a special program that runs the build process for your project. With this, CBuild provides convenient features that other tools lack, including a familiar programming language, debuggable programs, full control over the build process, and a flexible configuration system. If you're tired of CMake and Premake standing in your way and taking precious time, or frustrated with Make and Ninja's domain languages and MSBuild's incredibly slow compilation times, CBuild might be something you'd want to check out.

## Building project - is just another program.

Over the years, I have encountered numerous challenges when working with other build systems and tools. Here are a few of those stumbling blocks:

- Maintaining configurations at scale can become ridiculous because of poor DSL design.
- Adding custom logic to some build systems for your personal needs is challenging, if not impossible.
- There's no way to debug the configuration.
- Unreasonable defaults are hard to remove.
- There is a need to manage headers explicitly.

For a long time, I've been using shell scripts to build projects. It's easy to get started; you have full control over what exactly is passed to the compiler, and writing custom logic is a no-brainer. Unfortunately, with scale, this approach becomes inconvenient and limiting. You can write more scripts and build such infrastructure, but at that point, it would just mimic what Make has been doing for all these years.

I wanted a better tool, one where I could configure projects using a language I'm comfortable with, offering a straightforward API and providing the ability to write custom logic with ease. More importantly, it should grant full control over the build process and include a debugging feature to troubleshoot any issues promptly. At the core, my desire was to build projects using another program written in C, which led to the inception of CBuild.

## Example

Here's a minimal example to build this project:

```c
#include "cbuild.h"

extern "C" bool setup_project (const Arguments *args, Project *project) {
  Target *cbuild = add_executable(project, "cbuild");
  add_all_sources_from_directory(cbuild, "code", "cpp", false);
  return true;
}
```

Please check out [CBuild's](./project/build.cpp) configuration for a fuller example.

## API

You can see available function [here](./code/cbuild_api_template) or view the generated `cbuild.h` file in your `project` folder.

## Getting Started

- Download the binary
- Create a folder for the project and run `cbuild init`
  - This should create base files for the project and a main file to get you started
- Call `cbuild build` to build the project
- By default binary will be generated in `.cbuild\build\out\main.exe`

## Performance

Here are some numbers for this project.

The build script that I'm using to bootstrap the project calls the compiler and nothing else. On my Ryzen 7950X, it takes approximately 800ms after all file-system things are cached:

```
$ measure-command { ./build }
Compile:   0.682162 seconds
Link:      0.097300 seconds

TotalMilliseconds : 785.5088
```

For comparison, here are the numbers for building CBuild with GNU Make:

```
$ measure-command { make -j32 | out-host }

TotalMilliseconds : 928.7488
```

And here's an equivalent call for CBuild:

```
$ measure-command { cbuild build targets=cbuild cache=off | Write-Host }
CBuild r8
...
Linking target: cbuild
Finished in: 1338ms

TotalMilliseconds : 1348.6275
```

While it's clearly slower, this time can largely be attributed to the need to build the meta-program itself. After it's built and available, the build times for the project itself are roughly the following:

```
$ measure-command { cbuild build targets=cbuild cache=off | Write-Host }
CBuild r8
...
Linking target: cbuild
Finished in: 673ms

TotalMilliseconds : 688.0592
```

While CBuild shows better numbers across projects where I've used it, the main point here is that it's not slower than alternative programs.

P.S. Unfortunately, CMake and Premake were not able to produce a configuration that can build a C++ project on Windows using clang++. :shrug:
