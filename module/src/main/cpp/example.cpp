#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <jni.h>
#include <link.h> // 必须包含这个
#include "zygisk.hpp"

#define TAG "FGO_MOD_CN"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ==========================================
// 你的 Offset (B服)
// ==========================================
uintptr_t OFFSET_ATK = 0x24be874; 
uintptr_t OFFSET_HP  = 0x24a46f8;

// 回调结构体
struct callback_data {
    const char *name;
    uintptr_t base_addr;
};

// 系统级回调，不读文件，反作弊无法检测
static int dl_iterate_callback(struct dl_phdr_info *info, size_t size, void *data) {
    struct callback_data *cb_data = (struct callback_data *)data;
    if (info->dlpi_name && strstr(info->dlpi_name, cb_data->name)) {
        cb_data->base_addr = info->dlpi_addr;
        return 1; 
    }
    return 0; 
}

// 安全获取基址
void *get_base_address_safe(const char *name) {
    struct callback_data data;
    data.name = name;
    data.base_addr = 0;
    dl_iterate_phdr(dl_iterate_callback, &data);
    return (void *)data.base_addr;
}

// 安全 Patch
void patch_address(void *dest_addr) {
    if (dest_addr == nullptr) return;

    // MOV W0, #100000; RET
    unsigned char patch_code[] = {
        0x40, 0xD5, 0x90, 0x52, 
        0x20, 0x00, 0xA0, 0x72, 
        0xC0, 0x03, 0x5F, 0xD6  
    };

    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    // 1. RW
    if (mprotect(page_start, page_size, PROT_READ | PROT_WRITE) == -1) return;
    
    // 2. Copy
    memcpy(dest_addr, patch_code, sizeof(patch_code));
    
    // 3. RX
    mprotect(page_start, page_size, PROT_READ | PROT_EXEC);

    // 4. Clear Cache
    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + sizeof(patch_code));
}

// 线程逻辑
void *hack_thread(void *arg) {
    LOGD("Hack thread started.");
    
    void *il2cpp_base = nullptr;
    int max_retries = 120; // 60s
    
    // 循环等待库加载
    while (il2cpp_base == nullptr && max_retries > 0) {
        il2cpp_base = get_base_address_safe("libil2cpp.so");
        if (il2cpp_base == nullptr) {
            usleep(500000); 
            max_retries--;
        }
    }
    
    if (il2cpp_base == nullptr) {
        LOGE("libil2cpp.so not found.");
        return nullptr;
    }
    
    LOGD("Found libil2cpp: %p", il2cpp_base);
    
    // 强制等待 15 秒，跳过游戏启动检测期
    LOGD("Waiting 15s...");
    sleep(15); 

    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    
    LOGD("Patching...");
    patch_address(addr_atk);
    patch_address(addr_hp);
    LOGD("Done.");
    return nullptr;
}

class FgoModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (process) {
            // B服包名包含 fatego
            if (strstr(process, "fate")) {
                LOGD("Detected Bilibili FGO");
                pthread_t pt;
                pthread_create(&pt, nullptr, hack_thread, nullptr);
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
};

REGISTER_ZYGISK_MODULE(FgoModule)
