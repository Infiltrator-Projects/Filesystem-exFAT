/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_exfat_defrag_v1.h"
int ifs_bind_services(const IfsDefragServicesV1 *);
void ifs_unbind_services(void);
int ifs_exfat_worker_main(int, char **);
static int run(int argc, char **argv,const IfsDefragServicesV1 *host) {
 if (ifs_bind_services(host)!=0)return 1;
 const int result=ifs_exfat_worker_main(argc,argv);
 ifs_unbind_services();
 return result;
}
static const IfsExfatDefragV1 api={
 IFS_EXFAT_DEFRAG_ABI_V1,sizeof(IfsExfatDefragV1),
 "exfat",IFS_EXFAT_DEFRAG_CAP_RUN,run
};
__attribute__((visibility("default")))
const IfsExfatDefragV1 *infiltrator_filesystem_exfat_defrag_v1(void) {
 return &api;
}
