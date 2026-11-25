/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent exFAT single-library strict Common V2 capability contract.
 * Real media and on-disk mutation are exercised separately in realimage test.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <stdio.h>
extern const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void);
int main(void)
{
    const IfsFsModuleApiV2 *v2=infiltrator_filesystem_api_v2();
    if (!ifs_fs_api_v2_valid(v2,"exfat") ||
        v2->runtime_profiles!=IFS_FS_PROFILE_HOSTED ||
        v2->group_mask!=(IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                         IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)|
                         IFS_FS_GROUP_BIT(IFS_FS_GROUP_DEFRAG)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_ALLOCATION))) {
        fputs("Native exFAT Common V2 descriptor invalid\n",stderr);
        return 1;
    }
    const IfsFsCoreOpsV2 *core=
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(v2,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *volume=
        (const IfsFsVolumeOpsV2 *)ifs_fs_group_v2(v2,IFS_FS_GROUP_VOLUME);
    const IfsFsDefragOpsV2 *map=
        (const IfsFsDefragOpsV2 *)ifs_fs_group_v2(v2,IFS_FS_GROUP_DEFRAG);
    if (!map || map->header.capabilities!=(IFS_FS_DEFRAG_MAP|IFS_FS_DEFRAG_PLAN|IFS_FS_DEFRAG_EXECUTE|IFS_FS_DEFRAG_VERIFY|IFS_FS_DEFRAG_RECOVER) || !map->map ||
        !map->plan || !map->execute || !map->verify || !map->recover || !map->release_plan ||
        !core || !volume ||
        core->header.abi_version!=IFS_FS_GROUP_ABI_V2 ||
        core->header.capabilities!=IFS_FS_CORE_ALL ||
        !core->probe || !core->mount || !core->unmount ||
        !core->stat || !core->directory_entry || !core->read ||
        !core->write || !core->create || !core->remove ||
        !core->rename || !core->truncate ||
        volume->header.abi_version!=IFS_FS_GROUP_ABI_V2 ||
        volume->header.capabilities!=
            (IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_SYNC|IFS_FS_VOLUME_FORMAT|
             IFS_FS_VOLUME_LABEL_GET|IFS_FS_VOLUME_LABEL_SET|IFS_FS_VOLUME_CHECK) ||
        !volume->query || !volume->sync || !volume->format ||
        !volume->label || !volume->set_label || !volume->check || volume->repair ||
        ifs_fs_group_v2(v2,IFS_FS_GROUP_HANDLES) ||
        ifs_fs_group_v2(v2,IFS_FS_GROUP_METADATA) ||
        ifs_fs_group_v2(v2,IFS_FS_GROUP_STREAMS)) {
        fputs("exFAT V2 CORE/VOLUME callback mismatch\n",stderr);
        return 2;
    }
    puts("EXFAT-STRICT-COMMON-V2-CAPABILITIES-PASS");
    return 0;
}
