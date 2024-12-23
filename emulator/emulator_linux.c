// RainbowChina/SafeNET Microdog Emulator Logic for SDK 3.4 / 4.0
#include <stdio.h>
#include <memory.h>

#include "server_emulator.h"
#include "../common/memutils.h"

#define MD40_PACKET_SIZE 596
#define MD40_DAEMON_ADDR "/var/run/microdog/u.daemon"
#define MD40_SOCKET_MASK "/tmp/u.XXXXXX"

#define MD33_IOCTL 0x6B00
#define MD34_IOCTL 0x6B01

#define MD34_FAKE_FD 0x1337
#define MD34_PATH_USB "/dev/usbdog"
#define MD34_PATH_LPT "/dev/mhdog"


// -- MicroDog 3.4 API Logic
typedef int tioctl(int fd, unsigned long request, void* data);
typedef int topen(const char *path, int oflag);


static topen* real_open = NULL;
static tioctl * real_ioctl = NULL;

#define DIRECT_HOOK

#ifdef DIRECT_HOOK
#define md40_sendto sendto
#define md40_recvfrom recvfrom
#define md40_select select
#define md34_open open
#define md34_ioctl ioctl
#endif

void do_preloads();

int md34_open(const char *path, int oflag){
    if(real_open == NULL) do_preloads();
    // TODO: add !strcmp(path,MD34_PATH_LPT) back once we figure out wtf the deal is with LPT
    if(!strcmp(path,MD34_PATH_USB)){
        return MD34_FAKE_FD;
    }
    return real_open(path,oflag);
}


int md34_ioctl(int fd, int request, void * data){
    if(real_ioctl == NULL) do_preloads();
    if(fd == MD34_FAKE_FD){
        HandlePacket(*(unsigned int*)data);
        return 0;
    }
    return real_ioctl(fd,request,data);
}


// -- MicroDog 4.0 API Logic
typedef ssize_t tsendto(int socket, const void *message, size_t length, int flags, void *dest_addr, int dest_len);
typedef ssize_t trecvfrom(int socket, void *restrict buffer, size_t length, int flags, void *restrict address, int *restrict address_len);
typedef int tselect(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);
static tsendto * real_sendto = NULL;
static trecvfrom * real_recvfrom = NULL;
static tselect * real_select = NULL;

int md40_last_sock_fd = 0;
unsigned char md40_last_packet[MD40_PACKET_SIZE] = {0x00};
ssize_t md40_sendto(int socket, const void *message, size_t length, int flags, void *dest_addr, int dest_len){
    if(real_sendto == NULL) do_preloads();
    if(!memcmp(dest_addr+2,MD40_DAEMON_ADDR,strlen(MD40_DAEMON_ADDR))){
        md40_last_sock_fd = socket;
        memcpy(md40_last_packet,message,length);
        return length;
    }
    return real_sendto(socket,message,length,flags,dest_addr,dest_len);
}

ssize_t md40_recvfrom(int socket, void *restrict buffer, size_t length, int flags, void *restrict address, int *restrict address_len){
    if(real_recvfrom == NULL) do_preloads();
    if(!memcmp(address+2,MD40_DAEMON_ADDR,strlen(MD40_DAEMON_ADDR))){
        memcpy(buffer,md40_last_packet,MD40_PACKET_SIZE);
        memset(md40_last_packet,0x00,MD40_PACKET_SIZE);
        md40_last_sock_fd = -1;
        return MD40_PACKET_SIZE;
    }
    return real_recvfrom(socket,buffer,length,flags,address,address_len);
}

int md40_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout){
    if(real_select == NULL) do_preloads();
    if(nfds-1 == md40_last_sock_fd){
        HandlePacket(md40_last_packet);
        return 1;
    }
    return real_select(nfds,readfds, writefds, exceptfds, timeout);
}



// Startup Stuff
void __attribute__((constructor)) init_emulator_linux();

#define _GNU_SOURCE
#define __USE_GNU
#include <dlfcn.h>
void do_preloads(){
    // Honestly Battery, very clever, but not compatible with all libc versions
    // Will replace with just the good'old expose and call
    // Just because for regular executables, it will just search the
#ifdef DIRECT_HOOK
    // Hook the Following Syscalls for API v4.0
    real_sendto = dlsym(RTLD_NEXT, "sendto");
    real_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    real_select = dlsym(RTLD_NEXT, "select");
    real_open = dlsym(RTLD_NEXT, "open");
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    // Hook the Following Syscalls for API v3.x - sometimes, you can leave the target size at 0.
#else
    // Hook the Following Syscalls for API v4.0
    HotPatch_patch(/*"libc.so.6"*/NULL,"sendto",9,md40_sendto,(void**)&real_sendto);
    HotPatch_patch(/*"libc.so.6"*/NULL,"recvfrom",0x0A,md40_recvfrom,(void**)&real_recvfrom);
    HotPatch_patch(/*"libc.so.6"*/NULL,"select",0x0B,md40_select,(void**)&real_select);
    // Hook the Following Syscalls for API v3.x - sometimes, you can leave the target size at 0.
    HotPatch_patch(/*"libc.so.6"*/NULL,"open",10,md34_open,(void**)&real_open);
    HotPatch_patch(/*"libc.so.6"*/NULL,"ioctl",0x0A,md34_ioctl,(void**)&real_ioctl);
#endif
}

void init_emulator_linux(){
    InitEmulator();
    if(real_open == NULL) do_preloads();
}

