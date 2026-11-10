/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_exfat_defrag_v1.h"
#include "exfat_api.h"
#include <infiltratr/filesystem_module_v2.h>
extern const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void);
extern const IfsFsModuleApiV1 *infiltrator_filesystem_api_v1(void);
#include <string.h>
#include <stdio.h>
int main(void){
 const IfsFsModuleApiV1 *common=infiltrator_filesystem_api_v1();
 const IfsFsModuleApiV2 *v2=infiltrator_filesystem_api_v2();
 if(!ifs_fs_api_v2_valid(v2,"exfat") ||
    v2->base_v1!=common ||
    (v2->runtime_profiles&IFS_FS_PROFILE_KERNEL) ||
    !ifs_fs_group_v2(v2,IFS_FS_GROUP_VOLUME))return 1;
 {
    const IfsFsVolumeOpsV1 *ops =
        (const IfsFsVolumeOpsV1 *)ifs_fs_group_v2(
            v2, IFS_FS_GROUP_VOLUME);
    if(!ops || !(ops->header.capabilities & IFS_FS_VOLUME_QUERY) ||
       !ops->query)return 1;
 }
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
