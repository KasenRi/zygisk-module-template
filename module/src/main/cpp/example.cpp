#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <sys/mman.h>
#include <pthread.h>
#include <jni.h>
#include <string.h>
#include <link.h>
#include "zygisk.hpp"

// ==========================================
// TAG 保持为 FGO_MOD_TEST
// ==========================================
#define TAG "FGO_MOD_TEST"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// 待验证的地址（2.117.0 版本需要校准）
uintptr_t OFFSET_HP  = 0x24a7b20; 
uintptr_t OFFSET_ATK = 0x24be8bc; 

// 辅助函数：查找基址
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
void *get_base_address(const char *name) {
    struct callback_data data;
    data.name = name;
    data.base_addr = 0;
    dl_iterate_phdr(dl_iterate_callback, &data);
    return (void *)data.base_addr;
}

// 辅助函数：读取并打印内存（只读，不改）
void peek_memory(const char* label, void* base, uintptr_t offset) {
    if (!base) return;
    unsigned char* p = (unsigned char*)((uintptr_t)base + offset);
    
    // 读取前 12 个字节，足够我看清是什么指令了
    LOGD("[%s] Offset: 0x%lx | Hex: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
         label, offset, 
         p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
}

// ==========================================
// 注入线程
// ==========================================
void *hack_thread(void *arg) {
    LOGD("=== FGO MOD STARTED (Checking Offsets) ===");
    
    // 1. 等待 libil2cpp.so 加载
    void *il2cpp_base = nullptr;
    while (!il2cpp_base) {
        il2cpp_base = get_base_address("libil2cpp.so");
        sleep(1);
    }
    LOGD("libil2cpp found at: %p", il2cpp_base);

    // 2. 进游戏等待（给足够的时间让战斗逻辑加载）
    LOGD("Waiting 15 seconds...");
    sleep(15); 

    // 3. 读取内存
    peek_memory("HP_ADDR_DATA", il2cpp_base, OFFSET_HP);
    peek_memory("ATK_ADDR_DATA", il2cpp_base, OFFSET_ATK);
    
    // 4. 尝试向后探测（通常新版本地址会往后移一点点，比如 +0x100 到 +0x2000）
    peek_memory("GUESS_1", il2cpp_base, OFFSET_HP + 0x100);
    peek_memory("GUESS_2", il2cpp_base, OFFSET_HP + 0x500);

    LOGD("=== ANALYSIS DONE ===");
    return nullptr;
}

// ==========================================
// Zygisk 模块主体
// ==========================================
class FgoModule : public zygisk::ModuleBase {
private:
    zygisk::Api *api;
    JNIEnv *env;

public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        
        // 检测包名中是否包含 "fate"
        if (process && strstr(process, "fate")) {
            LOGD("Detected Game Process: %s", process);
            pthread_t pt;
            pthread_create(&pt, nullptr, hack_thread, nullptr);
        }
        
        env->ReleaseStringUTFChars(args->nice_name, process);
    }
};

REGISTER_ZYGISK_MODULE(FgoModule)
