#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <jni.h>
#include "zygisk.hpp"

#define TAG "FGO_MOD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ==========================================
// 偏移量配置
// ==========================================
uintptr_t OFFSET_ATK = 0x24be874;
uintptr_t OFFSET_HP  = 0x24a46f8;

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

// 核心 Patch 功能
void patch_address(void *dest_addr) {
    if (dest_addr == nullptr) return;

    // ARM64: MOV W0, #100000; RET
    unsigned char patch_code[] = {
        0x40, 0xD5, 0x90, 0x52, 
        0x20, 0x00, 0xA0, 0x72, 
        0xC0, 0x03, 0x5F, 0xD6  
    };

    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    // 修改权限
    if (mprotect(page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1) {
        LOGE("mprotect failed!");
        return;
    }
    
    // 写入
    memcpy(dest_addr, patch_code, sizeof(patch_code));
    // 刷新缓存
    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + sizeof(patch_code));
}

// 工作线程
void *hack_thread(void *arg) {
    LOGD("Hack thread running... Waiting for libil2cpp.so");
    
    void *il2cpp_base = nullptr;
    int max_retries = 60; // 尝试 60 次 (约30秒)
    
    while (il2cpp_base == nullptr && max_retries > 0) {
        il2cpp_base = get_base_address("libil2cpp.so");
        usleep(500000); // 0.5s
        max_retries--;
    }
    
    if (il2cpp_base == nullptr) {
        LOGE("Timed out! Could not find libil2cpp.so. Is the game protected?");
        return nullptr;
    }
    
    LOGD("Found libil2cpp at: %p", il2cpp_base);
    sleep(1); // 等待内存加载稳定

    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    
    LOGD("Applying Patches...");
    patch_address(addr_atk);
    patch_address(addr_hp);
    LOGD("Patch Applied Successfully!");
    return nullptr;
}

// Zygisk 入口
class FgoModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 使用 this->env 来调用 JNI 函数
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        
        if (process) {
            // 宽泛匹配：只要包名包含 "fate" 或者是 "fgom" (台服)，就注入
            if (strstr(process, "fate") || strstr(process, "fgom")) {
                LOGD("Detected Target Game: %s", process);
                pthread_t pt;
                pthread_create(&pt, nullptr, hack_thread, nullptr);
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }
    }

private:
    // ！！！ 之前就是漏了这下面两行声明 ！！！
    zygisk::Api *api;
    JNIEnv *env;
};

REGISTER_ZYGISK_MODULE(FgoModule)
