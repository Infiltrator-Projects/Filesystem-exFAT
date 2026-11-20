/* SPDX-License-Identifier: GPL-3.0-or-later
 * Deliberately re-checksum an invalid exFAT geometry on a disposable COPY.
 * The production reader must independently reject invalid FAT structure.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    int fd=open(argv[1],O_RDWR|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return 3;
    struct stat st;
    if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<128*1024*1024)
        return 4;
    uint8_t boot[512];
    if(pread(fd,boot,sizeof(boot),0)!=(ssize_t)sizeof(boot)||
       memcmp(boot+3,"EXFAT   ",8)!=0)return 5;
    const unsigned shift=boot[108];
    if(shift<9u||shift>12u)return 6;
    const size_t sector=(size_t)1u<<shift;
    uint8_t *data=calloc(12u,sector);
    if(!data)return 7;
    if(pread(fd,data,12u*sector,0)!=(ssize_t)(12u*sector))return 8;
    /* Set FATLength to zero while keeping the exFAT checksum VALID. */
    memset(data+84,0,4u);
    uint32_t sum=0;
    for(unsigned block=0;block<11u;++block)
        for(size_t i=0;i<sector;++i) {
            if(block==0u&&(i==106u||i==107u||i==112u))continue;
            sum=(sum>>1)|(sum<<31);
            sum+=data[block*sector+i];
        }
    for(size_t i=0;i<sector;i+=4u) {
        data[11u*sector+i]=(uint8_t)sum;
        data[11u*sector+i+1u]=(uint8_t)(sum>>8);
        data[11u*sector+i+2u]=(uint8_t)(sum>>16);
        data[11u*sector+i+3u]=(uint8_t)(sum>>24);
    }
    if(pwrite(fd,data,12u*sector,0)!=(ssize_t)(12u*sector)||
       fsync(fd)!=0)return 9;
    free(data);
    return close(fd)==0?0:10;
}
