#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
// ============ 修复部分 Start ============
#include <cstdio>   // 替代 fstream，更轻量
#include <cstdlib>  // 包含 strtoul
#include <cstring>  // 包含 strstr
// ============ 修复部分 End ============
#include <string>
#include <thread>
#include <vector>
#include "zygisk.hpp"

#define LOG_TAG "FGO_GOD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ==========================================
// 你的 OFFSETS
// ==========================================
uintptr_t OFFSET_DAMAGE = 0x24bbfb8; // get_DealtDamage
uintptr_t OFFSET_GUTS   = 0x24c1d90; // IsNotDeathDamage

// ==========================================
// 辅助工具：获取基址
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

// ==========================================
// 核心黑科技：直接写内存 (无需 Dobby)
// ==========================================
void patch_memory(uintptr_t absolute_addr, bool is_damage) {
    // 更改内存页权限为可写
    size_t page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page_start = absolute_addr & ~(page_size - 1);
    mprotect((void*)page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);

    if (is_damage) {
        // 目标：返回 999999 (0xF423F)
        uint32_t shellcode[] = {
            0x528847E0, // MOV W0, #0x423F (16959)
            0x72A001E0, // MOVK W0, #0xF, LSL#16 (result = 999999)
            0xD65F03C0  // RET
        };
        memcpy((void*)absolute_addr, shellcode, sizeof(shellcode));
        LOGD("FGO_GOD: 秒杀补丁已应用！");
    } else {
        // 目标：返回 True (1)
        uint32_t shellcode[] = {
            0x52800020, // MOV W0, #1
            0xD65F03C0  // RET
        };
        memcpy((void*)absolute_addr, shellcode, sizeof(shellcode));
        LOGD("FGO_GOD: 无敌补丁已应用！");
    }

    // 清除指令缓存
    __builtin___clear_cache((char*)absolute_addr, (char*)absolute_addr + 16);
}

void hack_thread() {
    LOGD("FGO_GOD: 等待 libil2cpp.so...");
    uintptr_t base_addr = 0;
    while ((base_addr = get_module_base("libil2cpp.so")) == 0) {
        usleep(100000);
    }
    
    // 延迟 2 秒，等游戏加载稳一点
    sleep(2);

    patch_memory(base_addr + OFFSET_DAMAGE, true);
    patch_memory(base_addr + OFFSET_GUTS, false);
}

// Zygisk 样板代码
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
            LOGD("FGO_GOD: 锁定目标！");
            api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        std::thread(hack_thread).detach();
    }
private:
    zygisk::Api *api;
    JNIEnv *env;
};
REGISTER_ZYGISK_MODULE(MyModule)
