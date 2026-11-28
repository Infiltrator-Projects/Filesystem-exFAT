cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILESYSTEM_VERSION LIMIT_COUNT 1)
project(FilesystemExFATLinux VERSION ${FILESYSTEM_VERSION} LANGUAGES C)
include(CTest)
include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
# Reuse the pinned checkout populated by the Make and native-installer entry
# points. Bare GitHub source archives can also configure directly via FetchContent.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/infiltratr-common/CMakeLists.txt" AND
   NOT FETCHCONTENT_SOURCE_DIR_INFILTRATR_COMMON)
 execute_process(COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/standalone/prepare-common.sh"
                 RESULT_VARIABLE common_status)
 if(NOT common_status EQUAL 0)
  message(FATAL_ERROR "The Common dependency did not match its source pin")
 endif()
 set(FETCHCONTENT_SOURCE_DIR_INFILTRATR_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/src/infiltratr-common")
endif()
FetchContent_Declare(infiltratr_common
 GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
 GIT_TAG 3c771c83a3b41bf57e8ca6527e7ec84465800381)
FetchContent_MakeAvailable(infiltratr_common)
# Common supplies link-time code, not independent exFAT package payloads.
# Installing the Common directory would leak unrelated headers, archives,
# CMake metadata and shared assets into direct filesystem installations.
set_property(DIRECTORY "${infiltratr_common_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
set(EX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/native/defrag")
set(FATFS "${CMAKE_CURRENT_SOURCE_DIR}/native/portable/fatfs")
set(EXAPI "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/exfat/module")
# Build a freestanding, no-import executable of the SAME exFAT engine,
# then embed that exact ELF in the hosted package carrier.
if(NOT CMAKE_OBJCOPY)
  find_program(CMAKE_OBJCOPY NAMES objcopy llvm-objcopy REQUIRED)
endif()
set(EXFAT_FIRMWARE_DIR "${CMAKE_CURRENT_BINARY_DIR}/firmware")
set(EXFAT_FIRMWARE_ELF "${EXFAT_FIRMWARE_DIR}/exFAT.so")
add_custom_command(OUTPUT "${EXFAT_FIRMWARE_ELF}"
  COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_SOURCE_DIR}/module/firmware"
    -B "${EXFAT_FIRMWARE_DIR}" -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCOMMON_INCLUDE_DIR=${infiltratr_common_SOURCE_DIR}/include"
  COMMAND "${CMAKE_COMMAND}" --build "${EXFAT_FIRMWARE_DIR}" --parallel 2
  DEPENDS module/firmware/CMakeLists.txt module/exfat_common.c
    native/filesystems/exfat/module/exfat_module.c
    native/portable/fatfs/fatfs_module.c
    native/portable/fatfs/fatfs_libc.c
    native/portable/fatfs/ff.c native/portable/fatfs/ffunicode.c
    native/portable/fatfs/ffconf.h
    "${infiltratr_common_SOURCE_DIR}/include/infiltratr/filesystem_module_v2.h"
  VERBATIM)
add_custom_target(exfat-firmware-elf DEPENDS "${EXFAT_FIRMWARE_ELF}")
# Common V2 is the ONLY shipped filesystem runtime.
# Retain source-owned historical defrag workers under native/defrag for
# separate migration and regression; their V1 host service tables and POSIX
# device/journal dependencies are not linked into exFAT.so or its DEB/RUN.
# The exposed V2 CORE and VOLUME operations are implemented by FatFs.
add_library(exfat-unified SHARED
 "${EXAPI}/exfat_module.c" module/exfat_common.c
 "${FATFS}/ff.c" "${FATFS}/ffunicode.c")
target_include_directories(exfat-unified PRIVATE
 "${EX_SRC}" "${FATFS}" "${EXAPI}" "${CMAKE_CURRENT_SOURCE_DIR}/compat"
 "${infiltratr_common_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(exfat-unified PRIVATE
 _GNU_SOURCE _FILE_OFFSET_BITS=64 IFS_FATFS_ONLY=64)
target_compile_options(exfat-unified PRIVATE
 -Wall -Wextra -Wpedantic -Werror -fvisibility=hidden)
target_link_options(exfat-unified PRIVATE -Wl,-z,defs
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module/v2-only.map")
set_property(TARGET exfat-unified APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/module/v2-only.map")
target_link_libraries(exfat-unified PRIVATE InfiltratrCommon::Common)
set_target_properties(exfat-unified PROPERTIES PREFIX "" OUTPUT_NAME "exFAT" SUFFIX ".so")
add_dependencies(exfat-unified exfat-firmware-elf)
set_property(TARGET exfat-unified APPEND PROPERTY LINK_DEPENDS "${EXFAT_FIRMWARE_ELF}")
add_custom_command(TARGET exfat-unified POST_BUILD
  COMMAND "${CMAKE_OBJCOPY}" --add-section
    ".infiltrator_boot=${EXFAT_FIRMWARE_ELF}"
    --set-section-flags .infiltrator_boot=readonly
    "$<TARGET_FILE:exfat-unified>"
  VERBATIM)
if(BUILD_TESTING)
 add_executable(exfat-unified-contract tests/test_unified_module.c)
 target_include_directories(exfat-unified-contract PRIVATE
  "${infiltratr_common_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${EXAPI}" "${FATFS}")
 target_link_libraries(exfat-unified-contract PRIVATE exfat-unified)
 add_test(NAME exfat-unified-module COMMAND exfat-unified-contract)
 add_test(NAME exfat-v2-only-public-exports
   COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_v2_exports.sh"
     "$<TARGET_FILE:exfat-unified>")
endif()
# Generate the manual from the same editable source used by the DEB and RUN.
set(EXFAT_GENERATED_MANUAL "${CMAKE_CURRENT_BINARY_DIR}/Filesystem-exFAT.pdf")
add_custom_command(
 OUTPUT "${EXFAT_GENERATED_MANUAL}"
 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/standalone/generate_manual.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" "${EXFAT_GENERATED_MANUAL}"
 DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
         "${CMAKE_CURRENT_SOURCE_DIR}/docs/exFAT.pdfstreams"
         "${CMAKE_CURRENT_SOURCE_DIR}/standalone/manual.py"
         "${CMAKE_CURRENT_SOURCE_DIR}/standalone/generate_manual.py"
 COMMENT "Generate current independent exFAT filesystem manual"
 VERBATIM)
add_custom_target(exfat-manual ALL DEPENDS "${EXFAT_GENERATED_MANUAL}")
install(TARGETS exfat-unified LIBRARY DESTINATION /System/Filesystems)
install(FILES "${EXFAT_GENERATED_MANUAL}"
        DESTINATION /System/Resources/Documentation)
