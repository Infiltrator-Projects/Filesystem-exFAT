cmake_minimum_required(VERSION 3.20)
project(FilesystemExFATLinux VERSION 1.0.0 LANGUAGES C)
include(CTest)
include(FetchContent)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
 GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
 GIT_TAG c76ec95ef66fc728df6a906ad75a6a4dd63b419b)
FetchContent_MakeAvailable(infiltratr_common)
set(EX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/native/defrag")
set(FAT "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/fat/portable")
set(EXAPI "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/exfat/module")
add_library(exfat-unified SHARED
 "${EXAPI}/exfat_module.c"
 "${FAT}/ff.c" "${FAT}/ffunicode.c"
 "${EX_SRC}/exfat_common.c" "${EX_SRC}/exfat_plan.c"
 "${EX_SRC}/exfat_relayout.c" "${EX_SRC}/exfat_worker.c"
 compat/legacy_services.c compat/runtime_alloc.c
 compat/exfat_extra_host.c module/exfat_defrag_module.c)
set_source_files_properties("${EX_SRC}/exfat_worker.c" PROPERTIES
 COMPILE_DEFINITIONS "main=ifs_exfat_worker_main")
target_include_directories(exfat-unified PRIVATE
 "${EX_SRC}" "${FAT}" "${EXAPI}" "${CMAKE_CURRENT_SOURCE_DIR}/compat"
 "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(exfat-unified PRIVATE
 _GNU_SOURCE _FILE_OFFSET_BITS=64 IFS_FATFS_ONLY=64)
target_compile_options(exfat-unified PRIVATE
 -Wall -Wextra -Wpedantic -Werror -fvisibility=hidden)
target_link_options(exfat-unified PRIVATE -Wl,-z,defs)
target_link_libraries(exfat-unified PRIVATE
 InfiltratrCommon::Common OpenSSL::Crypto SQLite::SQLite3 Threads::Threads)
set_target_properties(exfat-unified PROPERTIES PREFIX "" OUTPUT_NAME "exFAT" SUFFIX ".so")
if(BUILD_TESTING)
 add_executable(exfat-unified-contract tests/test_unified_module.c)
 target_include_directories(exfat-unified-contract PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${EXAPI}" "${FAT}")
 target_link_libraries(exfat-unified-contract PRIVATE exfat-unified)
 add_test(NAME exfat-unified-module COMMAND exfat-unified-contract)
endif()
install(TARGETS exfat-unified LIBRARY DESTINATION /System/Filesystems)
