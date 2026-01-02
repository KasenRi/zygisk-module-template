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
// 1. 这里的 TAG 改回你最熟悉的 FGO_MOD_TEST
// ==========================================
#define TAG "FGO_MOD_TEST"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// 你之前的地址（虽然偏了，但我们需要看它偏哪去了）
uintptr_t OFFSET_HP  = 0x24a7b20; 
uintptr_t OFFSET_ATK = 0x24be8bc; 

// 辅助函数：找基址
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

// 辅助函数：打印内存到底是什么
void peek_memory(const char* label, void* base, uintptr_t offset) {
    if (!base) return;
    unsigned char* p = (unsigned char*)((uintptr_t)base + offset);
    // 打印前8个字节的机器码，通过这个我就能算出真正的函数在哪
    LOGD("[%s] Offset: 0x%lx | Hex: %02X %02X %02X %02X %02X %02X %02X %02X", 
         label, offset, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

// ==========================================
// 主逻辑
// ==========================================
void *hack_thread(void *arg) {
    LOGD("=== FGO MOD STARTED (v2.117.0 Analysis) ===");
    
    // 1. 等待 libil2cpp 加载
    void *il2cpp_base = nullptr;
    while (!il2cpp_base) {
        il2cpp_base = get_base_address("libil2cpp.so");
        sleep(1);
    }
    LOGD("libil2cpp found at: %p", il2cpp_base);

    // 2. 等待进图
    LOGD("Waiting 15 seconds for game to load...");
    sleep(15); 

    // 3. 不修改，只读取！(防止之前的红血BUG)
    // 只要把这两行日志发给我，我就能算出修正后的 Offset
    peek_memory("HP_ADDR_DATA", il2cpp_base, OFFSET_HP);
    peek_memory("ATK_ADDR_DATA", il2cpp_base, OFFSET_ATK);
    
    // 顺便看一眼后面一点点的地方（通常新版本地址会往后移）
    peek_memory("HP_ADDR_GUESS", il2cpp_base, OFFSET_HP + 0x200); 

    LOGD("=== ANALYSIS DONE ===");
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
        // 这里保留之前的判断逻辑
        if (process && strstr(process, "fate")) {
            pthread_t pt;
            pthread_create(&pt, nullptr, hack_thread, nullptr);
        }
        env->ReleaseStringUTFChars(args->nice_name, process);
    }
};

REGISTER_ZYGISK_MODULE(FgoModule)
