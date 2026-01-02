#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <vector>

#define TAG "FGO_MOD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ==========================================
// 核心偏移量 (来自 dump.cs)
// ==========================================
uintptr_t OFFSET_ATK = 0x24be874;
uintptr_t OFFSET_HP  = 0x24a46f8;
// ==========================================

// 获取 libil2cpp.so 基地址
void *get_base_address(const char *name) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return nullptr;
    char line[512];
    void *addr = nullptr;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, name)) {
            sscanf(line, "%lx", (long *)&addr);
            break;
        }
    }
    fclose(fp);
    return addr;
}

// 核心功能：写入汇编代码 (Hex Patch)
// ARM64 指令：让函数直接返回 100,000
void patch_address(void *dest_addr) {
    // MOV W0, #34464 (0x86A0)
    // MOVK W0, #1, LSL #16 (总计 100000)
    // RET
    unsigned char patch_code[] = {
        0x40, 0xD5, 0x90, 0x52, 
        0x20, 0x00, 0xA0, 0x72, 
        0xC0, 0x03, 0x5F, 0xD6  
    };

    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    // 修改内存权限为 RWX (读写执行)
    mprotect(page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    
    // 写入机器码
    memcpy(dest_addr, patch_code, sizeof(patch_code));
    
    // 刷新缓存
    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + sizeof(patch_code));
}

// 主线程
void *hack_thread(void *arg) {
    LOGD("Hack thread started...");
    
    void *il2cpp_base = nullptr;
    while (il2cpp_base == nullptr) {
        il2cpp_base = get_base_address("libil2cpp.so");
        sleep(1);
    }
    
    LOGD("Found libil2cpp at: %p", il2cpp_base);
    
    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    
    LOGD("Patching ATK...");
    patch_address(addr_atk);
    
    LOGD("Patching HP...");
    patch_address(addr_hp);
    
    LOGD("Patch Complete! ATK/HP set to 100,000.");
    return nullptr;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    pthread_t pt;
    pthread_create(&pt, nullptr, hack_thread, nullptr);
    return JNI_VERSION_1_6;
}
