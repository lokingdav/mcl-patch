cmake_minimum_required(VERSION 3.18)

project(mcl
    VERSION 1.74
    LANGUAGES CXX C ASM)

if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# --- Configuration options ---
set(MCL_FP_BIT 384 CACHE STRING "max bit size for Fp")
set(MCL_FR_BIT 256 CACHE STRING "max bit size for Fr")

option(MCL_STANDALONE "build without standard library" OFF)
set(MCL_CFLAGS_STANDALONE
    -fno-threadsafe-statics
    -fno-exceptions
    -fno-rtti
    -DCYBOZU_DONT_USE_STRING
    -DCYBOZU_DONT_USE_EXCEPTION
    CACHE STRING "add user defined CFLAGS")

option(MCL_USE_GMP "Use GMP for the main library" OFF)
if(MSVC)
    set(MCL_TEST_WITH_GMP_DEFAULT OFF)
else()
    set(MCL_TEST_WITH_GMP_DEFAULT ON)
endif()
option(MCL_TEST_WITH_GMP "(Windows) download MPIR libraries from cybozulib_ext" ${MCL_TEST_WITH_GMP_DEFAULT})
option(MCL_USE_LLVM "use base64.ll with -DCMAKE_CXX_COMPILER=clang++" ON)
option(MCL_BUILD_SAMPLE "Build mcl samples" OFF)
option(MCL_BUILD_TESTING "Build mcl tests" OFF)

# --- Derive bit-size suffix ---
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BIT "64")
else()
    set(BIT "32")
endif()

# --- Output directories ---
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Core libraries ---
add_library(mcl SHARED src/fp.cpp)
add_library(mcl::mcl ALIAS mcl)

target_include_directories(mcl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>)

set_target_properties(mcl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME mcl
    VERSION ${mcl_VERSION}
    SOVERSION ${mcl_VERSION_MAJOR})

add_library(mcl_st STATIC src/fp.cpp)
add_library(mcl::mcl_st ALIAS mcl_st)

target_include_directories(mcl_st PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>)

set_target_properties(mcl_st PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME mcl)

# --- Compile definitions & options ---
target_compile_definitions(mcl    PUBLIC MCL_FP_BIT=${MCL_FP_BIT} MCL_FR_BIT=${MCL_FR_BIT})
target_compile_definitions(mcl_st PUBLIC MCL_FP_BIT=${MCL_FP_BIT} MCL_FR_BIT=${MCL_FR_BIT})

if(MSVC)
    if(MCL_MSVC_RUNTIME_DLL)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /MD /Oy /Ox /EHsc /GS- /Zi /DNDEBUG")
        set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS} /MDd")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /MT /Oy /Ox /EHsc /GS- /Zi /DNDEBUG")
        set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS} /MTd")
    endif()
    target_compile_definitions(mcl    PUBLIC NOMINMAX)
    target_compile_definitions(mcl_st PUBLIC NOMINMAX)
    set(MCL_COMPILE_OPTIONS /W4)
else()
    set(MCL_COMPILE_OPTIONS
        -Wall -Wextra -Wformat=2 -Wcast-qual -Wcast-align
        -Wwrite-strings -Wfloat-equal -Wpointer-arith
        -DNDEBUG -O3 -fPIC)
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

# --- Android cross-compile flags (when using NDK toolchain) ---
# These variables are set by CMake's Android toolchain file:
# - CMAKE_CXX_COMPILER_TARGET   e.g. aarch64-none-linux-android24
# - CMAKE_SYSROOT               path to sysroot
if(DEFINED CMAKE_CXX_COMPILER_TARGET AND DEFINED CMAKE_SYSROOT)
    set(ANDROID_TARGET_FLAG "--target=${CMAKE_CXX_COMPILER_TARGET}")
    set(ANDROID_SYSROOT_FLAG "--sysroot=${CMAKE_SYSROOT}")
else()
    set(ANDROID_TARGET_FLAG "")
    set(ANDROID_SYSROOT_FLAG "")
endif()

# --- Compile .ll bitcode files with proper target/sysroot ---
if(NOT (CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64") AND NOT (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND NOT APPLE))
    # bint${BIT}.ll → bint${BIT}.o
    set(BINT_OBJ "${CMAKE_CURRENT_BINARY_DIR}/bint${BIT}.o")
    target_compile_definitions(mcl    PUBLIC MCL_BINT_ASM_X64=0)
    target_compile_definitions(mcl_st PUBLIC MCL_BINT_ASM_X64=0)

    add_custom_command(OUTPUT ${BINT_OBJ}
        COMMAND ${CMAKE_CXX_COMPILER}
                ${ANDROID_TARGET_FLAG}
                ${ANDROID_SYSROOT_FLAG}
                -fPIC
                -c -o ${BINT_OBJ}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/bint${BIT}.ll
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_custom_target(gen_bint.o SOURCES ${BINT_OBJ})
    target_link_libraries(mcl     PUBLIC ${BINT_OBJ})
    add_dependencies(mcl     gen_bint.o)
    target_link_libraries(mcl_st PUBLIC ${BINT_OBJ})
    add_dependencies(mcl_st gen_bint.o)
    target_sources(mcl_st PRIVATE ${BINT_OBJ})

    # base${BIT}.ll → base${BIT}.o
    if(MCL_USE_LLVM)
        set(BASE_OBJ "${CMAKE_CURRENT_BINARY_DIR}/base${BIT}.o")
        target_compile_definitions(mcl    PUBLIC MCL_USE_LLVM=1)
        target_compile_definitions(mcl_st PUBLIC MCL_USE_LLVM=1)

        add_custom_command(OUTPUT ${BASE_OBJ}
            COMMAND ${CMAKE_CXX_COMPILER}
                    ${ANDROID_TARGET_FLAG}
                    ${ANDROID_SYSROOT_FLAG}
                    -fPIC
                    -c -o ${BASE_OBJ}
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/base${BIT}.ll
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        add_custom_target(gen_base.o SOURCES ${BASE_OBJ})
        target_link_libraries(mcl     PUBLIC ${BASE_OBJ})
        add_dependencies(mcl     gen_base.o)
        target_link_libraries(mcl_st PUBLIC ${BASE_OBJ})
        add_dependencies(mcl_st gen_base.o)
        target_sources(mcl_st PRIVATE ${BASE_OBJ})
    endif()
endif()

# --- AVX512 MSM routines on x86_64 Linux ---
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" AND NOT APPLE)
    set(MSM_OBJ "${CMAKE_CURRENT_BINARY_DIR}/msm_avx.o")

    add_custom_command(OUTPUT ${MSM_OBJ}
        COMMAND ${CMAKE_CXX_COMPILER}
                -c -o ${MSM_OBJ}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/msm_avx.cpp
                ${MCL_COMPILE_OPTIONS}
                -I ${CMAKE_CURRENT_SOURCE_DIR}/include
                -mavx512f -mavx512ifma
                -std=c++11 -fno-rtti
                -DCYBOZU_DONT_USE_STRING
                -DCYBOZU_DONT_USE_EXCEPTION
                -DNDEBUG
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_custom_target(msm_avx.o DEPENDS ${MSM_OBJ})
    target_link_libraries(mcl     PUBLIC ${MSM_OBJ})
    add_dependencies(mcl     msm_avx.o)
    target_link_libraries(mcl_st PUBLIC ${MSM_OBJ})
    add_dependencies(mcl_st msm_avx.o)
endif()

# --- Optional tests & samples ---
if(MCL_BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()
if(MCL_BUILD_SAMPLE)
    add_subdirectory(sample)
endif()
if(MCL_TEST_WITH_GMP AND NOT MSVC)
    find_package(GMP REQUIRED)
endif()

# --- Install/Export rules ---
install(TARGETS mcl mcl_st
    EXPORT mclTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION lib)

install(DIRECTORY include/mcl include/cybozu
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
