# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

#Copies compile_commands.json to root dir
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )
add_custom_target(
    copy-compile-commands ALL
    ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_CURRENT_LIST_DIR}
    )

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(detect)

# These samples use local static initialization. Since Zephyr doesn't support the
# C++ ABI for thread-safe initialization of local statics and the constructors don't
# appear to require thread safety, we turn it off in the C++ compiler.
set(NO_THREADSAFE_STATICS $<TARGET_PROPERTY:compiler-cpp,no_threadsafe_statics>)
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:${NO_THREADSAFE_STATICS}>)

file(GLOB app_sources src/* src/tflite/*)
target_sources(app PRIVATE ${app_sources})

