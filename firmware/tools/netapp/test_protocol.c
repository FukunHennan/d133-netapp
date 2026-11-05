/* SPDX-License-Identifier: Apache-2.0 */
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char archive[4096], payload[777], flashed[1024];
static size_t length, stored, begins;
static void entry(const char *name, const unsigned char *data, size_t n)
{
    char h[111]; unsigned sum = 0; size_t i;
    for (i=0;i<n;i++) sum += data[i];
    snprintf(h,sizeof(h),"070702%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
        1,0100644,0,0,1,0,(unsigned)n,0,0,0,0,(unsigned)strlen(name)+1,sum);
    memcpy(archive+length,h,110); length+=110;
    memcpy(archive+length,name,strlen(name)+1); length+=strlen(name)+1;
    while(length%4) archive[length++]=0;
    if(n) memcpy(archive+length,data,n);
    length+=n; while(length%4) archive[length++]=0;
}
static void make_archive(const char *name)
{
    unsigned char info[512] = {0};
    length=0; memset(archive,0,sizeof(archive));
    entry("ota_info.bin",info,sizeof(info)); entry(name,payload,sizeof(payload)); entry("TRAILER!!!",NULL,0);
    while(length%512) archive[length++]=0;
}
static int begin(void *arg, uint32_t size)
{ (void)arg; assert(size==sizeof(payload)); begins++; stored=0; return 0; }
static int write_data(void *arg, const void *data, size_t n)
{ (void)arg; assert(stored+n<=sizeof(flashed)); memcpy(flashed+stored,data,n); stored+=n; return 0; }
static int run(size_t step, size_t total, uint32_t cap, int *done)
{
    struct netapp_cpio p; size_t offset=0;
    netapp_cpio_init(&p,cap); p.begin=begin; p.write=write_data; begins=stored=0;
    while(offset<total) {
        size_t n=total-offset; if(n>step) n=step;
        if(netapp_cpio_feed(&p,archive+offset,n)) { *done=0; return -1; }
        offset+=n;
    }
    *done=p.done; return 0;
}
int main(int argc, char **argv)
{
    char ssid[33], password[64], url[256], crc[9], token[64]; size_t step,i; int done;
    if (argc == 2) {
        FILE *f=fopen(argv[1],"rb"); struct netapp_cpio p; unsigned char buf[2048]; size_t n;
        assert(f); netapp_cpio_init(&p,3328U*1024U);
        while ((n=fread(buf,1,sizeof(buf),f))!=0) assert(!netapp_cpio_feed(&p,buf,n));
        assert(!ferror(f)); fclose(f);
        assert(p.done&&p.entries==2&&p.os_size==p.written);
        printf("actual OTA archive validated: %u OS bytes\n",(unsigned)p.written); return 0;
    }
    assert(!netapp_form("ssid=My+WiFi&password=abc%261234",ssid,password));
    assert(!strcmp(ssid,"My WiFi")&&!strcmp(password,"abc&1234"));
    assert(!netapp_form("password=&ssid=%E4%B8%AD%E6%96%87",ssid,password));
    assert(netapp_form("ssid=&password=12345678",ssid,password));
    assert(netapp_form("ssid=test&password=short",ssid,password));
    assert(netapp_form("ssid=a%00b&password=12345678",ssid,password));
    assert(netapp_form("ssid=a%&password=12345678",ssid,password));
    assert(netapp_form("ssid=x&ssid=y&password=12345678",ssid,password));
    assert(netapp_form("ssid=123456789012345678901234567890123&password=",ssid,password));
    assert(netapp_form("ssid=x&password=&extra=bad",ssid,password));
    /* Web OTA trigger form: decode, required fields, duplicates, oversize. */
    assert(!netapp_ota_form("url=http%3A%2F%2F192.168.1.100%3A8000%2Fota.cpio&crc=13B78827&token=D133setup",url,crc,token));
    assert(!strcmp(url,"http://192.168.1.100:8000/ota.cpio")&&!strcmp(crc,"13B78827")&&!strcmp(token,"D133setup"));
    assert(!netapp_ota_form("url=http://x/y&crc=abcdef01&token=pw",url,crc,token));
    assert(netapp_ota_form("url=http://x/y&token=pw",url,crc,token));
    assert(netapp_ota_form("url=http://x/y&crc=&token=pw",url,crc,token));
    assert(netapp_ota_form("url=&crc=13B78827&token=pw",url,crc,token));
    assert(netapp_ota_form("url=http://x/y&crc=13B78827&token=",url,crc,token));
    assert(netapp_ota_form("url=http://x/y&crc=13B78827X&token=pw",url,crc,token));
    assert(netapp_ota_form("url=http://x/y&crc=13B78827&url=http://evil&token=pw",url,crc,token));
    assert(netapp_ota_form("url=http://x/y&crc=13B78827&token=pw&extra=1",url,crc,token));
    assert(netapp_ota_form("url=a%00b&crc=13B78827&token=pw",url,crc,token));
    assert(netapp_ota_form("nonsense",url,crc,token));
    for(i=0;i<sizeof(payload);i++) payload[i]=(unsigned char)i;
    make_archive("d13x_os.itb");
    for(step=1;step<=257;step++) {
        assert(!run(step,length,1024,&done)&&done);
        assert(begins==1&&stored==sizeof(payload)&&!memcmp(flashed,payload,sizeof(payload)));
    }
    assert(!run(1,700,1024,&done)&&!done); /* truncated OTA cannot commit */
    assert(run(17,length,128,&done)&&!begins); /* too large before erase */
    make_archive("../os"); assert(run(9,length,1024,&done)&&!begins);
    make_archive("rodata.fatfs"); assert(run(2048,length,1024,&done)&&!begins);
    make_archive("d13x_os.itb"); archive[900]^=1; assert(run(7,length,1024,&done));
    make_archive("d13x_os.itb"); archive[0]='x'; assert(run(3,length,1024,&done)&&!begins);
    puts("protocol: 257 fragmentation cases + malformed forms/OTA checks passed");
    return 0;
}
