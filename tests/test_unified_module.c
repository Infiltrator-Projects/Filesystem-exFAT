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
                         IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME))) {
        fputs("Native exFAT Common V2 descriptor invalid\n",stderr);
        return 1;
    }
    const IfsFsCoreOpsV2 *core=
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(v2,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *volume=
        (const IfsFsVolumeOpsV2 *)ifs_fs_group_v2(v2,IFS_FS_GROUP_VOLUME);
    if (!core || !volume ||
        core->header.abi_version!=IFS_FS_GROUP_ABI_V2 ||
        core->header.capabilities!=IFS_FS_CORE_ALL ||
        !core->probe || !core->mount || !core->unmount ||
        !core->stat || !core->directory_entry || !core->read ||
        !core->write || !core->create || !core->remove ||
        !core->rename || !core->truncate ||
        volume->header.abi_version!=IFS_FS_GROUP_ABI_V2 ||
        volume->header.capabilities!=
            (IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_SYNC|IFS_FS_VOLUME_FORMAT) ||
        !volume->query || !volume->sync || !volume->format ||
        ifs_fs_group_v2(v2,IFS_FS_GROUP_HANDLES) ||
        ifs_fs_group_v2(v2,IFS_FS_GROUP_METADATA) ||
        ifs_fs_group_v2(v2,IFS_FS_GROUP_DEFRAG) ||
        ifs_fs_group_v2(v2,IFS_FS_GROUP_STREAMS)) {
        fputs("exFAT V2 CORE/VOLUME callback mismatch\n",stderr);
        return 2;
    }
    puts("EXFAT-STRICT-COMMON-V2-CAPABILITIES-PASS");
    return 0;
}
