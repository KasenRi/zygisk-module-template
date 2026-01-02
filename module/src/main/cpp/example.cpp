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

#define TAG "FGO_MOD_TEST"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// 待验证地址
uintptr_t OFFSET_HP  = 0x24a7b20; 
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
void *get_base_address(const char *name) {
    struct callback_data data;
    data.name = name;
    data.base_addr = 0;
    dl_iterate_phdr(dl_iterate_callback, &data);
    return (void *)data.base_addr;
}

void peek_memory(const char* label, void* base, uintptr_t offset) {
    if (!base) return;
    unsigned char* p = (unsigned char*)((uintptr_t)base + offset);
    LOGD("[%s] Offset: 0x%lx | Hex: %02X %02X %02X %02X %02X %02X %02X %02X", 
         label, offset, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

// ==========================================
// 核心修改：真正的延迟启动
// ==========================================
void *hack_thread(void *arg) {
    // 【关键】线程启动后，强制睡 20 秒。
    // 这期间完全不占用资源，绝对不会卡死游戏启动。
    LOGD("=== FGO MOD STARTED: Sleeping 20s to avoid freeze ===");
    sleep(20); 
    
    LOGD("=== Waking up to find libil2cpp ===");

    void *il2cpp_base = get_base_address("libil2cpp.so");
    
    if (!il2cpp_base) {
        LOGD("Error: libil2cpp.so not found even after 20s!");
        return nullptr;
    }
    LOGD("libil2cpp found at: %p", il2cpp_base);

    // 再给 5 秒缓冲，确保你进入了战斗界面或者主界面
    sleep(5); 

    // 读取数据
    peek_memory("HP_ADDR_DATA", il2cpp_base, OFFSET_HP);
    peek_memory("ATK_ADDR_DATA", il2cpp_base, OFFSET_ATK);
    
    // 顺便向后探测一下
    peek_memory("GUESS_1", il2cpp_base, OFFSET_HP + 0x100);
    peek_memory("GUESS_2", il2cpp_base, OFFSET_HP + 0x200);

    LOGD("=== ANALYSIS DONE ===");
    return nullptr;
}

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
        if (process && strstr(process, "fate")) {
            pthread_t pt;
            pthread_create(&pt, nullptr, hack_thread, nullptr);
        }
        env->ReleaseStringUTFChars(args->nice_name, process);
    }
};

REGISTER_ZYGISK_MODULE(FgoModule)
