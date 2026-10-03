//
// Created by bytedance on 20.4.21.
//
#include "include/tools/utils.h"

#ifdef _WIN32
#include <io.h>
#ifndef F_OK
#define F_OK 0
#endif
#define access _access
#else
#include <unistd.h>
#endif

bool has_nvidia_gpu() {
#ifdef _WIN32
    return (access("C:\\Windows\\System32\\nvcuda.dll", F_OK) == 0);
#else
    return (access("/dev/nvidia0", F_OK) == 0) || (access("/dev/nvidiactl", F_OK) == 0);
#endif
}

bool has_amd_gpu() {
#ifdef _WIN32
    return (access("C:\\Windows\\System32\\amdhip64.dll", F_OK) == 0);
#else
    return (access("/dev/kfd", F_OK) == 0);
#endif
}
