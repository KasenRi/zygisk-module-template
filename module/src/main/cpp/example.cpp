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
// Offset 配置
// ==========================================
// 1. HP: getMaxHp (Int)
uintptr_t OFFSET_HP  = 0x24a7b20;

// 2. ATK: getCommandCardATK (Float) - 相比 getUpDownAtk 更稳定
uintptr_t OFFSET_ATK = 0x24be8bc;

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

void write_code(void *dest_addr, unsigned char *code, size_t size) {
    if (dest_addr == nullptr) return;

    unsigned int *original = (unsigned int *)dest_addr;
    LOGD("Patching: %p | Original Hex: %08X", dest_addr, *original);

    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    if (mprotect(page_start, page_size * 2, PROT_READ | PROT_WRITE) == -1) return;
    memcpy(dest_addr, code, size);
    if (mprotect(page_start, page_size * 2, PROT_READ | PROT_EXEC) == -1) return;

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
    LOGD("Waiting 10s...");
    sleep(10); 

    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    
    LOGD("Applying Final Balanced Patch...");

    // ---------------------------------------------------------
    // 1. HP Patch (Simple Int)
    // 效果: 所有人(敌我) HP = 100,000
    // ---------------------------------------------------------
    unsigned char patch_hp_100k[] = {
        0x00, 0xD4, 0x90, 0x52, // MOV W0, #34464
        0x20, 0x00, 0xA0, 0x72, // MOVK W0, #1, LSL 16
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };
    write_code(addr_hp, patch_hp_100k, sizeof(patch_hp_100k));

    // ---------------------------------------------------------
    // 2. ATK Patch (getCommandCardATK - Smart Float)
    // 逻辑: 
    //   if (isEnemy) return 10.0;   (打不动你的 10w 血)
    //   else         return 500000.0; (秒杀敌人的 10w 血)
    // ---------------------------------------------------------
    unsigned char patch_atk_smart[] = {
        // LDRB W10, [X0, #0x1F3] (读取 isEnemy)
        0x0A, 0x7C, 0x40, 0x39,
        // CBNZ W10, #12 (如果是敌人，跳到 Offset 20)
        0x6A, 0x00, 0x00, 0x35,
        
        // --- 玩家逻辑 (ATK = 500,000.0) ---
        // 500,000.0 Hex = 0x48F42400
        // MOV W0, #0x2400
        0x00, 0x48, 0x82, 0x52,
        // MOVK W0, #0x48F4, LSL 16
        0x80, 0xE8, 0xB1, 0x72,
        // B #8 (跳过敌人逻辑)
        0x02, 0x00, 0x00, 0x14,

        // --- 敌人逻辑 (ATK = 0.0) ---
        // MOV W0, #0
        0x00, 0x00, 0x80, 0x52,

        // --- 结束 ---
        // FMOV S0, W0 (转浮点)
        0x00, 0x28, 0x00, 0x1E,
        // RET
        0xC0, 0x03, 0x5F, 0xD6
    };
    write_code(addr_atk, patch_atk_smart, sizeof(patch_atk_smart));
    
    LOGD("Patch Applied! Everyone HP=100k. Player ATK=500k, Enemy ATK=0.");
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
