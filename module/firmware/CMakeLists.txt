# SPDX-License-Identifier: GPL-3.0-or-later
# This is the independently linked, no-libc Common V2 exFAT firmware profile.
# The format engine and V2 callbacks are the SAME source as hosted exFAT.so.
cmake_minimum_required(VERSION 3.20)
project(ExfatBootReader LANGUAGES C)
set(COMMON_INCLUDE_DIR "" CACHE PATH "Pinned Infiltratr Common include root")
if(NOT EXISTS "${COMMON_INCLUDE_DIR}/infiltratr/filesystem_module_v2.h")
  message(FATAL_ERROR "COMMON_INCLUDE_DIR must identify the same Common V2 headers as the hosted module")
endif()
set(FATFS "${CMAKE_CURRENT_SOURCE_DIR}/../../native/portable/fatfs")
set(EXAPI "${CMAKE_CURRENT_SOURCE_DIR}/../../native/filesystems/exfat/module")
add_library(exfat-boot MODULE
  ../exfat_common.c
  "${EXAPI}/exfat_module.c"
  "${FATFS}/fatfs_libc.c"
  "${FATFS}/ff.c"
  "${FATFS}/ffunicode.c")
set_target_properties(exfat-boot PROPERTIES
  PREFIX "" OUTPUT_NAME "exFAT" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_compile_features(exfat-boot PRIVATE c_std_11)
target_include_directories(exfat-boot PRIVATE "${COMMON_INCLUDE_DIR}" "${EXAPI}" "${FATFS}")
target_compile_definitions(exfat-boot PRIVATE
  IFS_FATFS_ONLY=64 IFS_EXFAT_FIRMWARE_BUILD=1)
target_compile_options(exfat-boot PRIVATE
  -Wall -Wextra -Wpedantic -Werror -ffreestanding -fno-builtin
  -fno-tree-loop-distribute-patterns -fno-stack-protector -mno-red-zone
  -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
target_link_options(exfat-boot PRIVATE
  -nostdlib -Wl,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now)
