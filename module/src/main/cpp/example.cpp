#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>
#include <jni.h>

// 必须包含这个头文件，这是 Zygisk 模块的灵魂
// 你的模板里肯定有这个文件，不用担心
#include "zygisk.hpp"

#define TAG "FGO_MOD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ==========================================
// 核心偏移量 (根据你的 dump.cs)
// ==========================================
uintptr_t OFFSET_ATK = 0x24be874;
uintptr_t OFFSET_HP  = 0x24a46f8;

// 获取 libil2cpp.so 基地址
void *get_base_address(const char *name) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return nullptr;
    char line[512];
    void *addr = nullptr;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, name)) {
            sscanf(line, "%lx", (long *)&addr);
            break;
        }
    }
    fclose(fp);
    return addr;
}

// 核心功能：写入汇编代码 (Hex Patch)
void patch_address(void *dest_addr) {
    // ARM64 汇编: MOV W0, #100000; RET
    unsigned char patch_code[] = {
        0x40, 0xD5, 0x90, 0x52, // MOV W0, 0x86A0 (低位)
        0x20, 0x00, 0xA0, 0x72, // MOVK W0, 1, LSL 16 (高位 -> 100000)
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };

    long page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    // 修改内存权限为 RWX
    mprotect(page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    // 写入
    memcpy(dest_addr, patch_code, sizeof(patch_code));
    // 刷新缓存
    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + sizeof(patch_code));
}

// 作弊线程
void *hack_thread(void *arg) {
    LOGD("Hack thread started. Waiting for libil2cpp.so...");
    
    void *il2cpp_base = nullptr;
    // 循环等待直到游戏加载了 il2cpp
    while (il2cpp_base == nullptr) {
        il2cpp_base = get_base_address("libil2cpp.so");
        usleep(500000); // 每 0.5 秒检查一次
    }
    
    LOGD("Found libil2cpp at: %p", il2cpp_base);
    sleep(1); // 再等1秒稳一点

    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    
    LOGD("Patching FGO...");
    patch_address(addr_atk);
    patch_address(addr_hp);
    
    LOGD("Success! ATK/HP set to 100,000.");
    return nullptr;
}

// ==========================================
// Zygisk 模块入口类 (之前缺的就是这个！)
// ==========================================
class FgoModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 1. 获取当前正在启动的 APP 包名
        const char *raw_process = env->GetStringUTFChars(args->nice_name, nullptr);
        bool is_target = false;

        // 2. 检查是不是 FGO
        // 只要包名里包含 "fategrandorder" 就认为是目标 (兼容日服和美服)
        // 如果你是 B服 (bilibili)，请把下面的字符串改成 "fatego"
        if (raw_process && strstr(raw_process, "fatego")) {
            is_target = true;
            LOGD("FGO Detected! Process: %s", raw_process);
        }

        env->ReleaseStringUTFChars(args->nice_name, raw_process);

        // 3. 如果是 FGO，就启动作弊线程
        if (is_target) {
            pthread_t pt;
            pthread_create(&pt, nullptr, hack_thread, nullptr);
        }
    }
};

// 注册 Zygisk 模块 (Magisk 靠这行代码识别模块)
REGISTER_ZYGISK_MODULE(FgoModule)
