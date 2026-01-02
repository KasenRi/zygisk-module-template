#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <errno.h>
#include "zygisk.hpp"

#define LOG_TAG "FGO_SAFE_INSPECT"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ==========================================
// 🔍 侦查目标 (来自 dump.cs)
// ==========================================
// 1. 当前血量 (get_hp)
uintptr_t OFFSET_HP  = 0x24a46f8; 

// 2. 造成伤害 (get_DealtDamage)
uintptr_t OFFSET_ATK = 0x24bbfb8;

// 3. 无敌判死 (IsNotDeathDamage)
uintptr_t OFFSET_GOD = 0x24c1d90;

// ==========================================
// 🛠️ 安全工具箱
// ==========================================

// 获取模块基址
uintptr_t get_module_base(const char* module_name) {
    FILE *fp;
    uintptr_t addr = 0;
    char filename[32], buffer[1024];
    snprintf(filename, sizeof(filename), "/proc/self/maps");
    fp = fopen(filename, "rt");
    if (fp != NULL) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (strstr(buffer, module_name)) {
                addr = (uintptr_t)strtoul(buffer, NULL, 16);
                break;
            }
        }
        fclose(fp);
    }
    return addr;
}

// 🛡️ 核心防爆盾：检测内存地址是否可读
// 返回 true 表示安全，返回 false 表示危险
bool is_address_safe(void* addr) {
    if (addr == nullptr) return false;
    
    // 获取页大小
    size_t page_size = sysconf(_SC_PAGESIZE);
    // 对齐到页边界
    void* page_start = (void*)((uintptr_t)addr & ~(page_size - 1));
    
    // 使用 msync 探测内存页是否映射。
    // 如果返回 -1 且 errno 是 ENOMEM，说明地址未映射，读取必崩。
    if (msync(page_start, page_size, MS_ASYNC) == -1 && errno == ENOMEM) {
        return false;
    }
    return true;
}

// 安全打印 Hex
void safe_inspect(const char* label, uintptr_t base, uintptr_t offset) {
    uintptr_t target_addr_val = base + offset;
    void* addr = (void*)target_addr_val;

    LOGD("🔍 准备检查 [%s] -> 计算地址: %p", label, addr);

    // 第一重保险：检查地址是否安全
    if (!is_address_safe(addr)) {
        LOGE("❌ 危险！地址 %p 未映射或非法，跳过读取以防止崩溃。", addr);
        return;
    }

    // 第二重保险：尝试读取
    unsigned char* p = (unsigned char*)addr;
    LOGD("✅ 地址有效，HEX 数据如下:");
    LOGD("   %02X %02X %02X %02X %02X %02X %02X %02X",
         p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

// ==========================================
// 🧵 侦查线程
// ==========================================
void inspect_thread() {
    LOGD("=== FGO 安全侦查模式启动 ===");
    
    uintptr_t base_addr = 0;
    // 等待加载，最多等 30 秒，防止死循环
    int retry = 0;
    while ((base_addr = get_module_base("libil2cpp.so")) == 0) {
        usleep(500000); // 0.5秒
        retry++;
        if (retry > 60) {
            LOGE("❌ 超时：未找到 libil2cpp.so，停止侦查。");
            return;
        }
    }
    LOGD("✅ 捕获 libil2cpp.so 基址: 0x%lx", base_addr);
    
    // 延迟 15 秒，给游戏充分的解密时间
    LOGD("⏳ 等待 15 秒让游戏解密内存...");
    sleep(15);

    // 开始安全检查
    safe_inspect("HP (get_hp)", base_addr, OFFSET_HP);
    safe_inspect("ATK (get_Damage)", base_addr, OFFSET_ATK);
    safe_inspect("GOD (IsNotDeath)", base_addr, OFFSET_GOD);

    LOGD("=== 侦察结束 ===");
}

// Zygisk 样板
class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 🛡️ 防砖补丁：防止空指针导致 Zygote 崩溃
        if (!args || !args->nice_name) return;

        const char *raw_process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (raw_process) {
            std::string process_name(raw_process);
            env->ReleaseStringUTFChars(args->nice_name, raw_process);

            // 只在目标进程注入
            if (process_name == "com.bilibili.fatego") {
                is_target = true;
                LOGD("🚀 锁定 FGO 进程，准备注入...");
                api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
            } else {
                api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            }
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (is_target) {
            std::thread(inspect_thread).detach();
        }
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    bool is_target = false;
};

REGISTER_ZYGISK_MODULE(MyModule)
