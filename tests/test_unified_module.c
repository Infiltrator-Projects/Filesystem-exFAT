/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_exfat_defrag_v1.h"
#include "exfat_api.h"
#include <infiltratr/filesystem_module.h>
extern const IfsFsModuleApiV1 *infiltrator_filesystem_api_v1(void);
#include <string.h>
#include <stdio.h>
int main(void){
 const IfsFsModuleApiV1 *common=infiltrator_filesystem_api_v1();
 if(!ifs_fs_common_api_valid(common,"exfat") ||
    !(common->capabilities & IFS_FS_CAP_FORMAT) ||
    !(common->capabilities & IFS_FS_CAP_READ) ||
    (common->capabilities & IFS_FS_CAP_RECOVER))return 1;
 const IfsExfatDefragV1 *api=infiltrator_filesystem_exfat_defrag_v1();
 const IfsExfatModuleV5 *files=infiltrator_filesystem_module_v5();
 if(!api || api->abi_version!=IFS_EXFAT_DEFRAG_ABI_V1 ||
    api->structure_bytes<sizeof(*api) || !api->format_id ||
    strcmp(api->format_id,"exfat") ||
    !(api->capabilities & IFS_EXFAT_DEFRAG_CAP_RUN) || !api->run ||
    !files || files->abi_version!=IFS_EXFAT_ABI_VERSION_V5 ||
    !files->compat_v4 || !files->read64 || !files->stat64){
  fputs("exFAT.so is missing defragmentation or full-lifecycle API\n",stderr);
  return 1;
 }
 return 0;
}
