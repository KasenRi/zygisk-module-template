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
// B服 Offset (保持不变)
// ==========================================
uintptr_t OFFSET_ATK = 0x24be874; 
uintptr_t OFFSET_HP  = 0x24a46f8;

struct callback_data {
    const char *name;
    uintptr_t base_addr;
};

// 安全获取基址
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

void patch_address(void *dest_addr) {
    if (dest_addr == nullptr) return;

    // 精确数值 100,000 (0x186A0)
    unsigned char patch_code[] = {
        0x00, 0xD4, 0x90, 0x52, // MOV W0, #34464
        0x20, 0x00, 0xA0, 0x72, // MOVK W0, #1, LSL #16
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };

    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    if (mprotect(page_start, page_size, PROT_READ | PROT_WRITE) == -1) {
        LOGE("mprotect RW failed");
        return;
    }
    
    memcpy(dest_addr, patch_code, sizeof(patch_code));
    
    if (mprotect(page_start, page_size, PROT_READ | PROT_EXEC) == -1) {
        LOGE("mprotect RX failed");
    }

    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + sizeof(patch_code));
}

// 线程逻辑
void *hack_thread(void *arg) {
    LOGD("Hack thread running...");
    
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
        LOGE("libil2cpp.so not found after waiting.");
        return nullptr;
    }
    
    LOGD("Found libil2cpp: %p. Waiting 15s safely...", il2cpp_base);
    
    // 此时游戏已经进入 Logo 或 标题，反作弊检测最严格的时候
    // 等待 15 秒以避开初始化检查
    sleep(15); 

    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    
    LOGD("Patching now...");
    patch_address(addr_atk);
    patch_address(addr_hp);
    LOGD("Patch Applied Successfully!");
    return nullptr;
}

class FgoModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    // 阶段1: 仅仅检测进程名，绝不启动线程
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (process) {
            if (strstr(process, "fate")) {
                is_target_app = true; // 标记这是目标应用
                LOGD("Target detected in pre-specialize. Standing by.");
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }
    }

    // 阶段2: 权限切换完毕，安全启动线程
    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (is_target_app) {
            LOGD("App specialized. Launching hack thread safely.");
            pthread_t pt;
            pthread_create(&pt, nullptr, hack_thread, nullptr);
        }
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    bool is_target_app = false; // 必须用成员变量传递状态
};

REGISTER_ZYGISK_MODULE(FgoModule)
