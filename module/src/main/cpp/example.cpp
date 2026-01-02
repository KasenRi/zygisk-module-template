#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "zygisk.hpp"

#define LOG_TAG "FGO_INSPECT"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ==========================================
// 🔍 待验证的 OFFSETS (来自 dump.cs)
// ==========================================
// 1. 当前血量 (get_hp) - 之前找错了，这次验证新的
uintptr_t OFFSET_HP  = 0x24a46f8; 

// 2. 造成伤害 (get_DealtDamage)
uintptr_t OFFSET_ATK = 0x24bbfb8;

// 3. 无敌判死 (IsNotDeathDamage)
uintptr_t OFFSET_GOD = 0x24c1d90;

// ==========================================
// 辅助工具
// ==========================================
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

// 打印内存 Hex，判断是不是函数头
void inspect_address(const char* label, uintptr_t base, uintptr_t offset) {
    void* addr = (void*)(base + offset);
    unsigned char* p = (unsigned char*)addr;

    // 尝试读取前 8 个字节
    // 注意：这里没有 try-catch，如果地址非法可能会崩，但如果是有效代码段通常没事
    LOGD("🔍 检查 [%s]", label);
    LOGD("   Offset: 0x%lx | 绝对地址: %p", offset, addr);
    LOGD("   Hex: %02X %02X %02X %02X %02X %02X %02X %02X",
         p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

// ==========================================
// 主线程
// ==========================================
void inspect_thread() {
    LOGD("=== FGO 侦探模式启动 ===");
    
    uintptr_t base_addr = 0;
    // 等待加载
    while ((base_addr = get_module_base("libil2cpp.so")) == 0) {
        usleep(100000);
    }
    LOGD("✅ 捕获 libil2cpp.so 基址: 0x%lx", base_addr);
    
    // 延迟 10 秒，确保游戏解密完成且内存稳定
    LOGD("⏳ 等待 10 秒让游戏完全加载...");
    sleep(10);

    // 开始检查
    inspect_address("HP (get_hp)", base_addr, OFFSET_HP);
    inspect_address("ATK (get_Damage)", base_addr, OFFSET_ATK);
    inspect_address("GOD (IsNotDeath)", base_addr, OFFSET_GOD);

    LOGD("=== 侦察结束，请查看日志分析 ===");
}

// Zygisk 样板
class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *raw_process = env->GetStringUTFChars(args->nice_name, nullptr);
        std::string process_name(raw_process);
        env->ReleaseStringUTFChars(args->nice_name, raw_process);

        if (process_name == "com.bilibili.fatego") {
            LOGD("FGO 启动，准备侦察...");
            api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        std::thread(inspect_thread).detach();
    }
private:
    zygisk::Api *api;
    JNIEnv *env;
};
REGISTER_ZYGISK_MODULE(MyModule)
