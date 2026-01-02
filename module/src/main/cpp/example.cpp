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

#define TAG "FGO_MOD_CN"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ==========================================
// 【根据你提供的 Dump 更新 Offset】
// ==========================================
// Target: public Single getUpDownAtk(...) -> 返回 Float
uintptr_t OFFSET_ATK = 0x24bef90; 
// Target: public Int32 getMaxHp() -> 返回 Int
uintptr_t OFFSET_HP  = 0x24a7b20;

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

// 通用的内存修改函数
void write_code(void *dest_addr, unsigned char *code, size_t size) {
    if (dest_addr == nullptr) return;

    // Debug: 打印原始字节确认 Offset 是否有效
    unsigned int *original = (unsigned int *)dest_addr;
    LOGD("Patching: %p | Original Hex: %08X", dest_addr, *original);

    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    if (mprotect(page_start, page_size, PROT_READ | PROT_WRITE) == -1) return;
    memcpy(dest_addr, code, size);
    if (mprotect(page_start, page_size, PROT_READ | PROT_EXEC) == -1) return;

    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + size);
}

void *hack_thread(void *arg) {
    LOGD("Hack thread initialized. Waiting for libil2cpp.so...");
    
    void *il2cpp_base = nullptr;
    int max_retries = 120; 
    while (il2cpp_base == nullptr && max_retries > 0) {
        il2cpp_base = get_base_address_safe("libil2cpp.so");
        if (il2cpp_base == nullptr) {
            usleep(500000); 
            max_retries--;
        }
    }
    
    if (il2cpp_base == nullptr) {
        LOGE("Timed out: libil2cpp.so not found.");
        return nullptr;
    }
    
    LOGD("Found libil2cpp at: %p", il2cpp_base);
    LOGD("Waiting 15s to bypass initial checks...");
    sleep(15); 

    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    
    LOGD("Start Patching with NEW Logic...");

    // ---------------------------------------------------------
    // 1. ATK Patch (针对 getUpDownAtk, 返回 Float)
    // 目标: 返回 100,000.0f (Hex: 0x47C35000)
    // ---------------------------------------------------------
    // MOV W0, #0x5000       -> 00 A0 80 52
    // MOVK W0, #0x47C3, LSL#16 -> 60 F8 A8 72
    // FMOV S0, W0           -> 00 28 00 1E  (将整数转入浮点寄存器 S0)
    // RET                   -> C0 03 5F D6
    unsigned char patch_atk_float[] = {
        0x00, 0xA0, 0x80, 0x52,
        0x60, 0xF8, 0xA8, 0x72,
        0x00, 0x28, 0x00, 0x1E,
        0xC0, 0x03, 0x5F, 0xD6
    };
    write_code(addr_atk, patch_atk_float, sizeof(patch_atk_float));

    // ---------------------------------------------------------
    // 2. HP Patch (针对 getMaxHp, 返回 Int)
    // 目标: 返回 100,000 (Hex: 0x186A0)
    // ---------------------------------------------------------
    // MOV W0, #34464        -> 00 D4 90 52
    // MOVK W0, #1, LSL #16  -> 20 00 A0 72
    // RET                   -> C0 03 5F D6
    unsigned char patch_hp_int[] = {
        0x00, 0xD4, 0x90, 0x52,
        0x20, 0x00, 0xA0, 0x72,
        0xC0, 0x03, 0x5F, 0xD6
    };
    write_code(addr_hp, patch_hp_int, sizeof(patch_hp_int));
    
    LOGD("Patch Applied! Testing getUpDownAtk (Float) and getMaxHp (Int).");
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
            if (strstr(process, "fate")) {
                is_target_app = true;
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (is_target_app) {
            pthread_t pt;
            pthread_create(&pt, nullptr, hack_thread, nullptr);
        }
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    bool is_target_app = false;
};

REGISTER_ZYGISK_MODULE(FgoModule)
