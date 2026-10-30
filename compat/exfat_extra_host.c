/* SPDX-License-Identifier: GPL-3.0-or-later
 * Filesystem-neutral POSIX adapters used only by standalone Linux SO worker.
 * The host still owns device identity through the bound identity callback.
 */
#define _GNU_SOURCE
#include "ld_device.h"
#include "ld_io.h"
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int ld_fd_format_identity(int fd,char *buffer,size_t capacity) {
 if(fd<0||!buffer||!capacity){errno=EINVAL;return -1;}
 struct stat status;
 if(fstat(fd,&status)!=0)return -1;
 const bool block=S_ISBLK(status.st_mode);
 if(!block && !S_ISREG(status.st_mode)){errno=EINVAL;return -1;}
 const LdDevice snapshot={
   .fd=fd,.path=NULL,.writable=false,.is_block=block,
   .size_bytes=0U,.device_number=block?status.st_rdev:0,
   .host_device=status.st_dev,.inode=status.st_ino
 };
 return ld_device_format_identity(&snapshot,buffer,capacity);
}
uint64_t ld_available_memory_bytes(void){
 FILE *file=fopen("/proc/meminfo","r");
 if(file){
  char key[64],unit[16];
  unsigned long long value;
  while(fscanf(file,"%63s %llu %15s",key,&value,unit)==3){
   if(strcmp(key,"MemAvailable:")==0){
    fclose(file);return (uint64_t)value*UINT64_C(1024);
   }
  }
  fclose(file);
 }
 const long pages=sysconf(_SC_AVPHYS_PAGES),page_size=sysconf(_SC_PAGESIZE);
 return pages>0&&page_size>0?
   (uint64_t)(unsigned long)pages*(uint64_t)(unsigned long)page_size:
   UINT64_C(512)*1024*1024;
}
size_t ld_default_ram_limit(void) {
 const uint64_t available=ld_available_memory_bytes();
 const uint64_t minimum=UINT64_C(64)*1024*1024;
 const uint64_t reserve=UINT64_C(8)*1024*1024*1024;
 const uint64_t maximum=UINT64_C(16)*1024*1024*1024;
 uint64_t selected=available>reserve+minimum?available-reserve:available/4;
 if(selected<minimum)selected=minimum;
 if(selected>maximum)selected=maximum;
 if(selected>SIZE_MAX)selected=SIZE_MAX;
 return (size_t)selected;
}
