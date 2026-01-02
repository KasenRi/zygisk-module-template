#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <jni.h>
#include <link.h>
#include "zygisk.hpp"

// 使用新的 TAG 方便过滤
#define TAG "FGO_MOD_INSPECT"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ==========================================
// 这里填入我们要侦测的地址
// 我根据 2.117.0 的大概范围，稍微调整了一下猜测范围
// 但主要还是看这些地址里到底是啥
// ==========================================
uintptr_t OFFSET_HP  = 0x24a7b20; // 之前的地址
uintptr_t OFFSET_ATK = 0x24be8bc; // 之前的地址

// ==========================================
// 辅助功能
// ==========================================

struct callback_data {
    const char *name;
    uintptr_t base_addr;
};

static int dl_iterate_callback(struct dl_phdr_info *info, size_t size, void *data) {
    struct callback_data *cb_data = (struct callback_data *)data;
    if (info->dlpi_name && strstr(info->dlpi_name, cb_data->name)) {
        cb_data->base_addr = info->dlpi_addr;
        return 1; 
    }
    return 0; 
}

void *get_base_address_safe(const char *name) {
    struct callback_data data;
    data.name = name;
    data.base_addr = 0;
    dl_iterate_phdr(dl_iterate_callback, &data);
    return (void *)data.base_addr;
}

// 打印内存 Hex
void print_hex(const char* label, void* base, uintptr_t offset) {
    if (base == nullptr) return;
    void* addr = (void*)((uintptr_t)base + offset);
    unsigned char* p = (unsigned char*)addr;
    
    // 打印 16 个字节
    LOGD("[%s] Offset: 0x%lx | Hex: %02X %02X %02X %02X %02X %02X %02X %02X", 
         label, offset, 
         p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

// ==========================================
// 主线程
// ==========================================

void *inspect_thread(void *arg) {
    LOGD("=== FGO INSPECTOR STARTED (v2.117.0 Check) ===");
    
    void *il2cpp_base = nullptr;
    while (!il2cpp_base) {
        il2cpp_base = get_base_address_safe("libil2cpp.so");
        sleep(1);
    }
    
    LOGD("Found libil2cpp. Base: %p", il2cpp_base);
    LOGD("Waiting 10 seconds to ensure game loaded...");
    sleep(10);
    
    // 侦测 1：原来的地址
    print_hex("HP_Check_Old",  il2cpp_base, OFFSET_HP);
    print_hex("ATK_Check_Old", il2cpp_base, OFFSET_ATK);

    // 侦测 2：稍微往后偏移一点（通常版本更新地址会后移）
    // 比如 +0x10000 左右
    print_hex("HP_Check_Guess1", il2cpp_base, OFFSET_HP + 0x15000); 
    print_hex("ATK_Check_Guess1", il2cpp_base, OFFSET_ATK + 0x15000);

    LOGD("=== INSPECTION DONE. Please copy the logs. ===");
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
        if (process && strstr(process, "fate")) is_target_app = true;
        if (process) env->ReleaseStringUTFChars(args->nice_name, process);
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (is_target_app) {
            pthread_t pt;
            pthread_create(&pt, nullptr, inspect_thread, nullptr);
        }
    }
private:
    zygisk::Api *api;
    JNIEnv *env;
    bool is_target_app = false;
};

REGISTER_ZYGISK_MODULE(FgoModule)
