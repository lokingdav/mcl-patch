cmake_minimum_required(VERSION 3.10)

project(mcl
    VERSION 1.74
    LANGUAGES CXX C ASM)

if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(MCL_FP_BIT 384 CACHE STRING "max bit size for Fp")
set(MCL_FR_BIT 256 CACHE STRING "max bit size for Fr")

option(
    MCL_STANDALONE
    "build without standard library"
    OFF
)

set(MCL_CFLAGS_STANDALONE -fno-threadsafe-statics -fno-exceptions -fno-rtti -DCYBOZU_DONT_USE_STRING -DCYBOZU_DONT_USE_EXCEPTION CACHE STRING "add user defined CFLAGS")

option(
    MCL_USE_GMP
    "Use GMP for the main library"
    OFF
)
if(MSVC)
    set(MCL_TEST_WITH_GMP_DEFAULT OFF)
else()
    set(MCL_TEST_WITH_GMP_DEFAULT ON)
endif()
option(
    MCL_TEST_WITH_GMP
    "(Windows) download MPIR libraries from cybozulib_ext"
    ${MCL_TEST_WITH_GMP_DEFAULT}
)
option(
    MCL_USE_LLVM
    "use base64.ll with -DCMAKE_CXX_COMPILER=clang++"
    ON
)
option(
    MCL_BUILD_SAMPLE
    "Build mcl samples"
    OFF
)
option(
    MCL_BUILD_TESTING
    "Build mcl tests"
    OFF
)
if(MSVC)
    option(
        MCL_MSVC_RUNTIME_DLL
        "use dynamic runtime /MD in msvc builds"
        OFF
    )
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BIT "64")
else()
    set(BIT "32")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(mcl SHARED src/fp.cpp)
add_library(mcl::mcl ALIAS mcl)
target_include_directories(mcl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:$CMAKE_INSTALL_DIR/include>)
set_target_properties(mcl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME mcl
    VERSION ${mcl_VERSION}
    SOVERSION ${mcl_VERSION_MAJOR})

add_library(mcl_st STATIC src/fp.cpp)
add_library(mcl::mcl_st ALIAS mcl_st)
target_include_directories(mcl_st PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:$CMAKE_INSTALL_DIR/include>)
set_target_properties(mcl_st PROPERTIES
    OUTPUT_NAME mcl
    POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(mcl PUBLIC MCL_FP_BIT=${MCL_FP_BIT} MCL_FR_BIT=${MCL_FR_BIT})
target_compile_definitions(mcl_st PUBLIC MCL_FP_BIT=${MCL_FP_BIT} MCL_FR_BIT=${MCL_FR_BIT})

if(MSVC)
    if(MCL_MSVC_RUNTIME_DLL)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /MD /Oy /Ox /EHsc /GS- /Zi /DNDEBUG")
        set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS} /MDd")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /MT /Oy /Ox /EHsc /GS- /Zi /DNDEBUG")
        set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS} /MTd")
    endif()
    target_compile_definitions(mcl PUBLIC NOMINMAX)
    target_compile_definitions(mcl_st PUBLIC NOMINMAX)
    set(MCL_COMPILE_OPTIONS /W4)
else()
    set(MCL_COMPILE_OPTIONS -Wall -Wextra -Wformat=2 -Wcast-qual -Wcast-align
        -Wwrite-strings -Wfloat-equal -Wpointer-arith -DNDEBUG -O3 -fPIC)
endif()

if(MCL_USE_GMP)
    list(APPEND MCL_COMPILE_OPTIONS -DMCL_USE_GMP=1)
    target_link_libraries(mcl     PRIVATE GMP::GMP)
    target_link_libraries(mcl_st PRIVATE GMP::GMP)
endif()

if(MCL_STANDALONE)
    set(MCL_COMPILE_OPTIONS ${MCL_COMPILE_OPTIONS} ${MCL_CFLAGS_STANDALONE})
endif()

target_compile_options(mcl    PRIVATE ${MCL_COMPILE_OPTIONS})
target_compile_options(mcl_st PRIVATE ${MCL_COMPILE_OPTIONS})

# use bint-x64 on x64, bint${BIT}.ll on the other CPU
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")  # Win64
    find_program(ML64 ml64.exe HINTS "${cl_path}" DOC "path to assembler")
    set(BINT_X64_OBJ "${CMAKE_CURRENT_BINARY_DIR}/bint-x64-win.obj")
    add_custom_command(OUTPUT ${BINT_X64_OBJ}
        COMMAND ${ML64} /c /Fo ${BINT_X64_OBJ} ${CMAKE_CURRENT_SOURCE_DIR}/src/asm/bint-x64-win.asm
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_custom_target(gen_bint-x64-win.obj SOURCES ${BINT_X64_OBJ})
    target_link_libraries(mcl     PUBLIC ${BINT_X64_OBJ})
    add_dependencies(mcl     gen_bint-x64-win.obj)
    target_link_libraries(mcl_st PUBLIC ${BINT_X64_OBJ})
    add_dependencies(mcl_st gen_bint-x64-win.obj)
    target_sources(mcl_st PRIVATE ${BINT_X64_OBJ})
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND NOT APPLE)
    if(CMAKE_SYSTEM_NAME STREQUAL "MSYS")
        target_sources(mcl    PRIVATE src/asm/bint-x64-mingw.S)
        target_sources(mcl_st PRIVATE src/asm/bint-x64-mingw.S)
    else()
        target_sources(mcl    PRIVATE src/asm/bint-x64-amd64.S)
        target_sources(mcl_st PRIVATE src/asm/bint-x64-amd64.S)
    endif()
else()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "requiring clang++. cmake -DCMAKE_CXX_COMPILER=clang++ ..")
    endif()
    set(BINT_OBJ "${CMAKE_CURRENT_BINARY_DIR}/bint${BIT}.o")
    message("bint_obj=" ${BINT_OBJ})
    target_compile_definitions(mcl    PUBLIC MCL_BINT_ASM_X64=0)
    target_compile_definitions(mcl_st PUBLIC MCL_BINT_ASM_X64=0)

    add_custom_command(OUTPUT ${BINT_OBJ}
        COMMAND ${CMAKE_CXX_COMPILER}
                ${CMAKE_CXX_FLAGS}
                -c -o ${BINT_OBJ}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/bint${BIT}.ll
                -O3 -fPIC
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_custom_target(gen_bint.o SOURCES ${BINT_OBJ})
    target_link_libraries(mcl     PUBLIC ${BINT_OBJ})
    add_dependencies(mcl     gen_bint.o)
    target_link_libraries(mcl_st PUBLIC ${BINT_OBJ})
    add_dependencies(mcl_st gen_bint.o)
    target_sources(mcl_st PRIVATE ${BINT_OBJ})
endif()

# use generated asm or compile base${BIT}.ll by clang
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "MSYS")
    set(X86_64_LINUX TRUE)
else()
    set(X86_64_LINUX FALSE)
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")  # Win64
    # skip
elseif(X86_64_LINUX)
    target_compile_definitions(mcl    PUBLIC MCL_USE_LLVM=1)
    target_compile_definitions(mcl_st PUBLIC MCL_USE_LLVM=1)
    target_sources(mcl    PRIVATE src/asm/x86-64.S)
    target_sources(mcl_st PRIVATE src/asm/x86-64.S)
elseif(MCL_USE_LLVM)
    set(BASE_OBJ "${CMAKE_CURRENT_BINARY_DIR}/base${BIT}.o")
    message("base_obj=" ${BASE_OBJ})

    target_compile_definitions(mcl    PUBLIC MCL_USE_LLVM=1)
    target_compile_definitions(mcl_st PUBLIC MCL_USE_LLVM=1)

    add_custom_command(OUTPUT ${BASE_OBJ}
        COMMAND ${CMAKE_CXX_COMPILER}
                ${CMAKE_CXX_FLAGS}
                -c -o ${BASE_OBJ}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/base${BIT}.ll
                -O3 -fPIC
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_custom_target(gen_base.o SOURCES ${BASE_OBJ})
    target_link_libraries(mcl     PUBLIC ${BASE_OBJ})
    add_dependencies(mcl     gen_base.o)
    target_link_libraries(mcl_st PUBLIC ${BASE_OBJ})
    add_dependencies(mcl_st gen_base.o)
    target_sources(mcl_st PRIVATE ${BASE_OBJ})
endif()

# Tests
if(MCL_BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

if(MCL_BUILD_SAMPLE)
    add_subdirectory(sample)
endif()

install(TARGETS mcl mcl_st
    EXPORT mclTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION lib)

install(DIRECTORY include/mcl
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
    PATTERN "curve_type.h"
    PATTERN "bn.h"
    PATTERN "bn_c256.h"
    PATTERN "bn_c384_256.h"
    PATTERN "bn_c384.h")

install(DIRECTORY include/cybozu
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp")

install(EXPORT mclTargets
    FILE mclTargets.cmake
    NAMESPACE mcl::
    DESTINATION lib/cmake/mcl)

export(EXPORT mclTargets
    FILE mclTargets.cmake
    NAMESPACE mcl::)
set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
export(PACKAGE mcl)