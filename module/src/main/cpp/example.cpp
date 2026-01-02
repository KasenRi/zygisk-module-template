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
// Target: public Int32 getMaxHp() 
// 我们只改 HP，不改 ATK，防止闪退
uintptr_t OFFSET_HP  = 0x24a7b20;

// Field Offset: public Boolean isEnemy; // 0x1f3
// 这是一个非常关键的偏移，用于判断是敌是友
#define OFFSET_IS_ENEMY 0x1F3

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
    
    // 需要足够的空间，防止指令越界
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
    
    LOGD("Applying SMART HP Patch (GodMode + OneHitKill)...");

    // ---------------------------------------------------------
    // 智能 HP Patch (ARM64 Assembly)
    // 逻辑：
    // 1. 读取 this->isEnemy (Offset 0x1F3)
    // 2. 如果是敌人 (isEnemy != 0) -> 返回 1 (秒杀)
    // 3. 如果是玩家 (isEnemy == 0) -> 返回 100,000 (无敌)
    // ---------------------------------------------------------
    unsigned char patch_hp_smart[] = {
        // 1. 读取 isEnemy 标志位到 W10 寄存器
        // LDRB W10, [X0, #0x1F3]  (Hex: 0A 7C 40 39)
        0x0A, 0x7C, 0x40, 0x39,

        // 2. 检查 W10 是否为 0 (0=玩家, 1=敌人)
        // CBZ W10, #0xC (如果是0，跳转到下方第3条指令之后，即跳过敌人逻辑)
        // Offset 12 bytes = 3 instructions
        0x6A, 0x00, 0x00, 0x34,

        // --- 敌人逻辑 (HP = 1) ---
        // MOV W0, #1
        0x20, 0x00, 0x80, 0x52,
        // RET
        0xC0, 0x03, 0x5F, 0xD6,

        // --- 玩家逻辑 (HP = 100,000) ---
        // MOV W0, #0x86A0 (34464)
        0x00, 0xD4, 0x90, 0x52,
        // MOVK W0, #1, LSL 16 (Result = 100,000)
        0x20, 0x00, 0xA0, 0x72,
        // RET
        0xC0, 0x03, 0x5F, 0xD6
    };

    write_code(addr_hp, patch_hp_smart, sizeof(patch_hp_smart));
    
    LOGD("Patch Applied! Enemy MaxHP=1, Player MaxHP=100k.");
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
