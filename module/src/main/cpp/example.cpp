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
// 【重要】 B服 Offset 配置区
// 请确保这两个地址是当前最新版本的 Offset
// ==========================================
uintptr_t OFFSET_ATK = 0x24be874; 
uintptr_t OFFSET_HP  = 0x24a46f8;

// 用于 dl_iterate_phdr 的回调结构
struct callback_data {
    const char *name;
    uintptr_t base_addr;
};

// 安全获取基址的回调函数
static int dl_iterate_callback(struct dl_phdr_info *info, size_t size, void *data) {
    struct callback_data *cb_data = (struct callback_data *)data;
    if (info->dlpi_name && strstr(info->dlpi_name, cb_data->name)) {
        cb_data->base_addr = info->dlpi_addr;
        return 1; // 找到了，停止遍历
    }
    return 0; 
}

// 封装后的获取基址函数
void *get_base_address_safe(const char *name) {
    struct callback_data data;
    data.name = name;
    data.base_addr = 0;
    dl_iterate_phdr(dl_iterate_callback, &data);
    return (void *)data.base_addr;
}

// 核心修改函数
void patch_address(void *dest_addr) {
    if (dest_addr == nullptr) return;

    // 【调试功能】读取并打印原始的 4 个字节
    // 用来判断 Offset 是否正确：
    // 如果是 00000000 -> Offset 错误（空内存）
    // 如果是 A9BF7BFD (STP X29...) -> 看起来像函数头
    unsigned int *original = (unsigned int *)dest_addr;
    LOGD("Patching: %p | Original Hex: %08X", dest_addr, *original);

    // ARM64 汇编指令：返回 100,000 (0x186A0)
    // MOV W0, #34464        -> 00 D4 90 52
    // MOVK W0, #1, LSL #16  -> 20 00 A0 72
    // RET                   -> C0 03 5F D6
    unsigned char patch_code[] = {
        0x00, 0xD4, 0x90, 0x52, 
        0x20, 0x00, 0xA0, 0x72, 
        0xC0, 0x03, 0x5F, 0xD6  
    };

    long page_size = sysconf(_SC_PAGESIZE);
    // 计算内存页对齐地址
    void *page_start = (void *)((uintptr_t)dest_addr & ~(page_size - 1));
    
    // 修改内存权限为 可写
    if (mprotect(page_start, page_size, PROT_READ | PROT_WRITE) == -1) {
        LOGE("mprotect RW failed");
        return;
    }
    
    // 写入 Patch 代码
    memcpy(dest_addr, patch_code, sizeof(patch_code));
    
    // 恢复内存权限为 可执行
    if (mprotect(page_start, page_size, PROT_READ | PROT_EXEC) == -1) {
        LOGE("mprotect RX failed");
    }

    // 刷新 CPU 缓存，确保新指令生效
    __builtin___clear_cache((char *)dest_addr, (char *)dest_addr + sizeof(patch_code));
}

// 注入线程的主逻辑
void *hack_thread(void *arg) {
    LOGD("Hack thread initialized. Waiting for libil2cpp.so...");
    
    void *il2cpp_base = nullptr;
    int max_retries = 120; // 最多等 60 秒
    
    // 循环检查 libil2cpp.so 是否加载
    while (il2cpp_base == nullptr && max_retries > 0) {
        il2cpp_base = get_base_address_safe("libil2cpp.so");
        if (il2cpp_base == nullptr) {
            usleep(500000); // 等 0.5 秒
            max_retries--;
        }
    }
    
    if (il2cpp_base == nullptr) {
        LOGE("Timed out: libil2cpp.so not found.");
        return nullptr;
    }
    
    LOGD("Found libil2cpp at: %p", il2cpp_base);
    LOGD("Waiting 15s to bypass initial checks...");
    
    // 延时 15 秒，避开游戏启动时的反作弊扫描
    sleep(15); 

    // 计算实际地址
    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);
    
    LOGD("Start Patching...");
    
    // 执行修改
    patch_address(addr_atk);
    patch_address(addr_hp);
    
    LOGD("Patch Applied! Please check values in game.");
    return nullptr;
}

// Zygisk 模块主类
class FgoModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    // 阶段1: 进程刚创建（还未沙盒化）
    // 这里只做检测，绝对不要启动线程，否则会卡死！
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (process) {
            // 检测包名中是否包含 fate
            if (strstr(process, "fate")) {
                is_target_app = true;
                LOGD("Target app detected: %s", process);
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        }
    }

    // 阶段2: 进程初始化完成（沙盒已应用）
    // 这里启动线程是安全的
    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (is_target_app) {
            LOGD("Launching hack thread in postAppSpecialize...");
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
