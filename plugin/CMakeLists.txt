cmake_minimum_required(VERSION 3.20)
project(tmplgen_plugins LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})

option(BUILD_CLANG_PLUGIN "Build the Clang frontend plugin" ON)
option(BUILD_CLANGD_PLUGIN "Build the clangd FeatureModule plugin" OFF)
set(CLANGD_SOURCE_DIR "" CACHE PATH
    "Path to llvm-project/clang-tools-extra/clangd")

if(NOT BUILD_CLANG_PLUGIN AND NOT BUILD_CLANGD_PLUGIN)
    message(FATAL_ERROR "Enable BUILD_CLANG_PLUGIN and/or BUILD_CLANGD_PLUGIN")
endif()

file(GLOB TMPLGEN_CORE_SOURCES CONFIGURE_DEPENDS ../src/*.c)
add_library(tmplgen-plugin-core OBJECT ${TMPLGEN_CORE_SOURCES})
set_target_properties(tmplgen-plugin-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
target_compile_definitions(tmplgen-plugin-core PRIVATE CLANGPLUGIN)
target_include_directories(tmplgen-plugin-core SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS})
target_include_directories(tmplgen-plugin-core PRIVATE ../src)

if(BUILD_CLANG_PLUGIN)
    add_library(tmplgen-clang-plugin MODULE
        clang_plugin.cpp
        $<TARGET_OBJECTS:tmplgen-plugin-core>)
    target_include_directories(tmplgen-clang-plugin SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS})
    target_compile_definitions(tmplgen-clang-plugin PRIVATE CLANGPLUGIN)
    if(NOT LLVM_ENABLE_RTTI)
        target_compile_options(tmplgen-clang-plugin PRIVATE -fno-rtti)
    endif()
    if(NOT LLVM_ENABLE_EH)
        target_compile_options(tmplgen-clang-plugin PRIVATE -fno-exceptions)
    endif()
    target_link_libraries(tmplgen-clang-plugin PRIVATE clang-cpp libclang)
    set_target_properties(tmplgen-clang-plugin PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        PREFIX "lib"
        OUTPUT_NAME "tmplgen")
endif()

if(BUILD_CLANGD_PLUGIN)
    if(NOT CLANGD_SOURCE_DIR OR
       NOT EXISTS "${CLANGD_SOURCE_DIR}/FeatureModule.h")
        message(FATAL_ERROR
            "BUILD_CLANGD_PLUGIN requires CLANGD_SOURCE_DIR to point at "
            "llvm-project/clang-tools-extra/clangd")
    endif()
    add_library(tmplgen-clangd-plugin MODULE
        clangd_plugin.cpp
        $<TARGET_OBJECTS:tmplgen-plugin-core>)
    target_include_directories(tmplgen-clangd-plugin SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
        "${CLANGD_SOURCE_DIR}"
        "${CLANGD_SOURCE_DIR}/../include-cleaner/include")
    target_compile_definitions(tmplgen-clangd-plugin PRIVATE CLANGDPLUGIN)
    if(NOT LLVM_ENABLE_RTTI)
        target_compile_options(tmplgen-clangd-plugin PRIVATE -fno-rtti)
    endif()
    if(NOT LLVM_ENABLE_EH)
        target_compile_options(tmplgen-clangd-plugin PRIVATE -fno-exceptions)
    endif()
    target_link_libraries(tmplgen-clangd-plugin PRIVATE clang-cpp libclang)
    set_target_properties(tmplgen-clangd-plugin PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        PREFIX "lib"
        OUTPUT_NAME "tmplgen-clangd")
endif()
