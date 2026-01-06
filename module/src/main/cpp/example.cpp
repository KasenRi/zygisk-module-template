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

#define LOG_TAG "FGO_GOD_MODE"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ==========================================
// 🎯 最终确认的 OFFSETS (基于 dump.cs 和 侦查结果)
// ==========================================
// 1. 造成伤害 (get_DealtDamage)
// 之前侦查验证过，地址正确
uintptr_t OFFSET_DAMAGE = 0x24bbfb8;

// 2. 无敌判死 (IsNotDeathDamage)
uintptr_t OFFSET_GOD = 0x24c1d90;

// 3. 敌我判断 (isEnemy 字段偏移)
// 这是一个非常关键的偏移量，决定了是秒杀还是自杀
#define OFFSET_IS_ENEMY 0x1f3

// ==========================================
// 🛠️ 内存写入工具
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

// 修改内存权限并写入 Shellcode
void patch_code(uintptr_t addr, const std::vector<uint32_t>& shellcode) {
    size_t page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page_start = addr & ~(page_size - 1);
    
    // 修改内存权限为可写 (Read | Write | Execute)
    mprotect((void*)page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    
    // 写入指令
    uint32_t* target = (uint32_t*)addr;
    for (size_t i = 0; i < shellcode.size(); i++) {
        target[i] = shellcode[i];
    }
    
    // 清除指令缓存，确保 CPU 执行新的指令
    __builtin___clear_cache((char*)addr, (char*)addr + shellcode.size() * 4);
}

// ==========================================
// 💉 核心注入逻辑
// ==========================================
void hack_thread() {
    LOGD("🚀 FGO 上帝模式启动...");
    
    uintptr_t base_addr = 0;
    // 循环等待 libil2cpp.so 加载
    while ((base_addr = get_module_base("libil2cpp.so")) == 0) {
        usleep(100000); // 0.1秒
    }
    
    LOGD("✅ libil2cpp.so 基址: 0x%lx", base_addr);
    LOGD("⏳ 等待 5 秒，确保游戏逻辑初始化完毕...");
    sleep(5);

    // ====================================================
    // Patch 1: 秒杀 + 敌人0伤害 (Hook get_DealtDamage)
    // ====================================================
    // 汇编逻辑翻译：
    // 1. 读取 isEnemy (this + 0x1f3)
    // 2. 如果是 0 (我方) -> 返回 999999
    // 3. 如果是 1 (敌方) -> 返回 0
    // ====================================================
    std::vector<uint32_t> damage_shellcode = {
        0x39407C09, // LDRB W9, [X0, #499] (读取 isEnemy, 499 = 0x1f3)
        0x7100013F, // CMP W9, #0          (比较 W9 和 0)
        0x54000060, // B.EQ #12            (如果是 0，跳转到下方第6条指令)
        
        // --- 敌方逻辑 (Branch 1) ---
        0x52800000, // MOV W0, #0          (伤害 = 0)
        0xD65F03C0, // RET                 (返回)
        
        // --- 我方逻辑 (Branch 2) ---
        0x528847E0, // MOV W0, #0x423F     (载入 16959)
        0x72A001E0, // MOVK W0, #0xF, LSL #16 (组合成 999999)
        0xD65F03C0  // RET                 (返回)
    };
    
    patch_code(base_addr + OFFSET_DAMAGE, damage_shellcode);
    LOGD("🔥 秒杀补丁已应用 (带敌我识别)！");


    // ====================================================
    // Patch 2: 我方无敌 (Hook IsNotDeathDamage)
    // ====================================================
    // 汇编逻辑翻译：
    // 1. 读取 isEnemy
    // 2. 如果是 0 (我方) -> 返回 True (不死)
    // 3. 如果是 1 (敌方) -> 返回 False (正常死亡)
    // ====================================================
    std::vector<uint32_t> god_shellcode = {
        0x39407C09, // LDRB W9, [X0, #499] (读取 isEnemy)
        0x7100013F, // CMP W9, #0          (比较)
        0x54000060, // B.EQ #12            (跳转)
        
        // --- 敌方逻辑 ---
        0x52800000, // MOV W0, #0          (返回 False / 0)
        0xD65F03C0, // RET
        
        // --- 我方逻辑 ---
        0x52800020, // MOV W0, #1          (返回 True / 1)
        0xD65F03C0  // RET
    };

    patch_code(base_addr + OFFSET_GOD, god_shellcode);
    LOGD("🛡️ 无敌补丁已应用 (带敌我识别)！");
    
    LOGD("✨ 所有的修改已完成，请进本体验！");
}

// ==========================================
// Zygisk 模版入口
// ==========================================
class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) return;
        const char *raw_process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (raw_process) {
            std::string process_name(raw_process);
            env->ReleaseStringUTFChars(args->nice_name, raw_process);

            // ⚠️ 注意：这里是 B 服包名，如果是日服/台服请自行修改
            if (process_name == "com.bilibili.fatego") {
                is_target = true;
                api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
            } else {
                api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            }
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (is_target) {
            // 启动注入线程
            std::thread(hack_thread).detach();
        }
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    bool is_target = false;
};

REGISTER_ZYGISK_MODULE(MyModule)
