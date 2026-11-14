cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILESYSTEM_VERSION LIMIT_COUNT 1)
project(FilesystemExFATLinux VERSION ${FILESYSTEM_VERSION} LANGUAGES C)
include(CTest)
include(FetchContent)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
 GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
 GIT_TAG b50f493d789b840edea40ea09978d94d751533f8)
FetchContent_MakeAvailable(infiltratr_common)
# Common supplies link-time code, not independent exFAT package payloads.
# Installing the Common directory would leak unrelated headers, archives,
# CMake metadata and shared assets into direct filesystem installations.
set_property(DIRECTORY "${infiltratr_common_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
set(EX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/native/defrag")
set(FATFS "${CMAKE_CURRENT_SOURCE_DIR}/native/portable/fatfs")
set(EXAPI "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/exfat/module")
add_library(exfat-unified SHARED
 "${EXAPI}/exfat_module.c" module/exfat_common.c
 "${FATFS}/ff.c" "${FATFS}/ffunicode.c"
 "${EX_SRC}/exfat_common.c" "${EX_SRC}/exfat_plan.c"
 "${EX_SRC}/exfat_relayout.c" "${EX_SRC}/exfat_worker.c"
 compat/legacy_services.c compat/runtime_alloc.c
 compat/exfat_extra_host.c)
set_source_files_properties("${EX_SRC}/exfat_worker.c" PROPERTIES
 COMPILE_DEFINITIONS "main=ifs_exfat_worker_main")
target_include_directories(exfat-unified PRIVATE
 "${EX_SRC}" "${FATFS}" "${EXAPI}" "${CMAKE_CURRENT_SOURCE_DIR}/compat"
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
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${EXAPI}" "${FATFS}")
 target_link_libraries(exfat-unified-contract PRIVATE exfat-unified)
 add_test(NAME exfat-unified-module COMMAND exfat-unified-contract)
endif()
# Generate the manual from the same editable source used by the DEB and RUN.
set(EXFAT_GENERATED_MANUAL "${CMAKE_CURRENT_BINARY_DIR}/Filesystem-exFAT.pdf")
add_custom_command(
 OUTPUT "${EXFAT_GENERATED_MANUAL}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/standalone/generate_manual.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" "${EXFAT_GENERATED_MANUAL}"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
         "${CMAKE_CURRENT_SOURCE_DIR}/docs/exFAT.pages"
         "${CMAKE_CURRENT_SOURCE_DIR}/standalone/manual.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/standalone/generate_manual.py"
 COMMENT "Generate current independent exFAT filesystem manual"
 VERBATIM)
add_custom_target(exfat-manual ALL DEPENDS "${EXFAT_GENERATED_MANUAL}")
install(TARGETS exfat-unified LIBRARY DESTINATION /System/Filesystems)
install(FILES "${EXFAT_GENERATED_MANUAL}"
        DESTINATION /System/Resources/Documentation)
