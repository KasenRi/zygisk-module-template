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

#define TAG "FGO_MOD_TEST"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ==========================================
// Offset 配置
// ==========================================
uintptr_t OFFSET_HP  = 0x24a7b20; // getMaxHp
uintptr_t OFFSET_ATK = 0x24be8bc; // getCommandCardATK

// ----------------------------------------------------------------
// [A] HP = 100,000 (Int)
// ----------------------------------------------------------------
unsigned char hex_hp_100k[] = {
    0x00, 0xD4, 0x90, 0x52, // MOV W0, #34464
    0x20, 0x00, 0xA0, 0x72, // MOVK W0, #1, LSL 16
    0xC0, 0x03, 0x5F, 0xD6  // RET
};

// ----------------------------------------------------------------
// [B] ATK = 20,000.0 (Float) - 安全固定值测试
// 20000.0 = 0x469C4000
// ----------------------------------------------------------------
unsigned char hex_atk_fixed_20k[] = {
    0x00, 0x80, 0x89, 0x52, // MOV W0, #0x4C00
    0x80, 0x39, 0xA6, 0x72, // MOVK W0, #0x469C, LSL 16
    0x00, 0x28, 0x00, 0x1E, // FMOV S0, W0
    0xC0, 0x03, 0x5F, 0xD6  // RET
};

// ----------------------------------------------------------------
// [C] Smart ATK (Try Register X1)
// 假设 svt 数据在 X1 寄存器中
// ----------------------------------------------------------------
unsigned char hex_atk_smart_x1[] = {
    0x2A, 0x7C, 0x40, 0x39, // LDRB W10, [X1, #0x1F3]  <-- Changed to X1
    0x6A, 0x00, 0x00, 0x35, // CBNZ W10, +12 bytes
    // Player (500k)
    0x00, 0x48, 0x82, 0x52, 
    0x80, 0xE8, 0xB1, 0x72, 
    0x02, 0x00, 0x00, 0x14, 
    // Enemy (0)
    0x00, 0x00, 0x80, 0x52, 
    // End
    0x00, 0x28, 0x00, 0x1E, 
    0xC0, 0x03, 0x5F, 0xD6  
};

// ----------------------------------------------------------------
// [D] Smart ATK (Try Register X2)
// 假设 svt 数据在 X2 寄存器中
// ----------------------------------------------------------------
unsigned char hex_atk_smart_x2[] = {
    0x4A, 0x7C, 0x40, 0x39, // LDRB W10, [X2, #0x1F3]  <-- Changed to X2
    0x6A, 0x00, 0x00, 0x35, 
    // Player (500k)
    0x00, 0x48, 0x82, 0x52, 
    0x80, 0xE8, 0xB1, 0x72, 
    0x02, 0x00, 0x00, 0x14, 
    // Enemy (0)
    0x00, 0x00, 0x80, 0x52, 
    // End
    0x00, 0x28, 0x00, 0x1E, 
    0xC0, 0x03, 0x5F, 0xD6  
};


// ==========================================
// Helper Functions
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

void write_code(void *dest_addr, unsigned char *code, size_t size) {
    if (dest_addr == nullptr) return;
    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    mprotect(page_start, page_size * 2, PROT_READ | PROT_WRITE);
    memcpy(dest_addr, code, size);
    mprotect(page_start, page_size * 2, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + size);
}

// ==========================================
// Main Thread
// ==========================================

void *hack_thread(void *arg) {
    LOGD("=== FGO MULTI-STAGE TESTER ===");
    
    void *il2cpp_base = nullptr;
    while (!il2cpp_base) {
        il2cpp_base = get_base_address_safe("libil2cpp.so");
        sleep(1);
    }
    
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    
    LOGD("Found libil2cpp. Base: %p", il2cpp_base);
    
    // -------------------------------------------------
    // Phase 0: Start (Wait 30s)
    // -------------------------------------------------
    LOGD("PHASE 0: Original Game. Please enter battle now! (Waiting 30s)");
    sleep(30);

    // -------------------------------------------------
    // Phase 1: HP Patch Only
    // -------------------------------------------------
    LOGD("PHASE 1: Applying HP=100k Patch...");
    write_code(addr_hp, hex_hp_100k, sizeof(hex_hp_100k));
    LOGD("PHASE 1: Applied. Check HP bars. (Waiting 30s)");
    sleep(30);

    // -------------------------------------------------
    // Phase 2: ATK Fixed 20k (Safety Check)
    // -------------------------------------------------
    LOGD("PHASE 2: Applying FIXED ATK = 20,000 Patch...");
    LOGD("TRY ATTACKING NOW. If it freezes here, the ATK OFFSET is wrong.");
    LOGD("If damage is 20000, then Offset is CORRECT.");
    write_code(addr_atk, hex_atk_fixed_20k, sizeof(hex_atk_fixed_20k));
    sleep(30);

    // -------------------------------------------------
    // Phase 3: Smart ATK (Assuming X1 Register)
    // -------------------------------------------------
    LOGD("PHASE 3: Applying Smart ATK (Trying Register X1)...");
    LOGD("TRY ATTACKING. If it freezes, X1 is wrong.");
    write_code(addr_atk, hex_atk_smart_x1, sizeof(hex_atk_smart_x1));
    sleep(30);

    // -------------------------------------------------
    // Phase 4: Smart ATK (Assuming X2 Register)
    // -------------------------------------------------
    LOGD("PHASE 4: Applying Smart ATK (Trying Register X2)...");
    LOGD("TRY ATTACKING. If Phase 3 froze, maybe this works?");
    write_code(addr_atk, hex_atk_smart_x2, sizeof(hex_atk_smart_x2));
    
    LOGD("TEST FINISHED.");
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
            pthread_create(&pt, nullptr, hack_thread, nullptr);
        }
    }
private:
    zygisk::Api *api;
    JNIEnv *env;
    bool is_target_app = false;
};

REGISTER_ZYGISK_MODULE(FgoModule)
