/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_exfat_defrag_v1.h"
#include "exfat_api.h"
#include <string.h>
#include <stdio.h>
int main(void){
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
