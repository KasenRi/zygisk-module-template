#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <cstdlib>
#include <pthread.h>
#include "zygisk.hpp"

// =================配置区域=================
#define LOG_TAG "FGO_MOD"
// 目标游戏包名 (B服: com.bilibili.fatego | 日服: com.aniplex.fategrandorder)
// 这里默认写 B服，如果是日服请修改
const char *TARGET_PACKAGE = "com.bilibili.fatego";
// =========================================

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// 通用内存 Patch 函数
// 作用：修改指定内存地址的指令权限，并写入新的机器码
void patch_bytes(uintptr_t address, const unsigned char* code, size_t len) {
    long page_size = sysconf(_SC_PAGE_SIZE);
    // 计算内存页对齐的起始地址
    void *page_start = (void *)(address & ~(page_size - 1));

    // 修改内存权限为：可读、可写、可执行 (RWX)
    if (mprotect(page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1) {
        LOGD("Error: mprotect 失败，无法修改内存权限!");
        return;
    }

    // 写入机器码
    memcpy((void *)address, code, len);

    // 刷新 CPU 指令缓存 (非常重要，否则 CPU 可能还在执行旧指令)
    __builtin___clear_cache((char *)address, (char *)address + len);
    
    LOGD("Patch 成功: 地址 %p, 长度 %zu", (void*)address, len);
}

// 核心外挂线程
void *hack_thread(void *arg) {
    LOGD("=== FGO 外挂线程启动 ===");
    LOGD("正在等待 libil2cpp.so 加载...");

    void *base_addr = nullptr;

    // 1. 循环检测，直到 libil2cpp.so 被游戏加载进内存
    while (base_addr == nullptr) {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                // 在内存映射表中查找 il2cpp
                if (strstr(line, "libil2cpp.so")) {
                    // 解析基地址 (十六进制)
                    base_addr = (void *)strtoul(line, nullptr, 16);
                    break;
                }
            }
            fclose(fp);
        }
        // 如果没找到，稍微睡一会儿再找，防止占满 CPU
        usleep(500000); // 500ms
    }

    LOGD("捕获 libil2cpp.so 基址: %p", base_addr);

    // =======================================================
    // 功能 1: 伤害修改 (One Hit Kill)
    // 目标函数: get_DealtDamage (Offset: 0x24bbfb8)
    // 效果: 强制返回 999,999
    // =======================================================
    
    uintptr_t offset_damage = 0x24bbfb8; // <--- 你提供的 Offset
    uintptr_t target_addr = (uintptr_t)base_addr + offset_damage;

    // ARM64 机器码 Payload: return 999999;
    // MOVZ W0, #0x423F      (0x423F = 16959)
    // MOVK W0, #0xF, LSL 16 (0xF0000 + 0x423F = 0xF423F = 999999)
    // RET
    unsigned char damage_payload[] = {
        0xE0, 0x47, 0x88, 0x52, 
        0xE0, 0x01, 0xA0, 0x72, 
        0xC0, 0x03, 0x5F, 0xD6  
    };

    LOGD("正在应用 [秒杀] Patch...");
    patch_bytes(target_addr, damage_payload, sizeof(damage_payload));


    // =======================================================
    // 功能 2: 无敌/不死 (God Mode) - 暂时注释掉
    // 目标函数: IsNotDeathDamage (Offset: 0x24c1d90)
    // 效果: 强制返回 1 (True)，即“这次伤害不会致死”
    // 注意：如果这函数是通用的，敌人可能也会不死，所以先测试秒杀
    // =======================================================
    /*
    uintptr_t offset_god = 0x24c1d90;
    unsigned char god_payload[] = {
        0x20, 0x00, 0x80, 0x52, // MOV W0, #1
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };
    LOGD("正在应用 [无敌] Patch...");
    patch_bytes((uintptr_t)base_addr + offset_god, god_payload, sizeof(god_payload));
    */

    LOGD("=== 所有 Patch 执行完毕，请进战斗测试 ===");
    return nullptr;
}

// Zygisk 模块入口类
class FgoModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // 获取当前进程名称
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        
        if (process && strcmp(process, TARGET_PACKAGE) == 0) {
            // 如果是 FGO，强制加入排除列表的卸载逻辑 (为了 Hook 能生效)
            api->setOption(zygisk::Option::FORCE_DENY_LIST_UNMOUNT);
            LOGD("发现目标进程 FGO，准备注入...");
        }
        
        env->ReleaseStringUTFChars(args->nice_name, process);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        
        if (process && strcmp(process, TARGET_PACKAGE) == 0) {
            // 只有在 FGO 进程内才启动外挂线程
            pthread_t t;
            pthread_create(&t, nullptr, hack_thread, nullptr);
            LOGD("线程已创建");
        }
        
        env->ReleaseStringUTFChars(args->nice_name, process);
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
};

// 注册模块
REGISTER_ZYGISK_MODULE(FgoModule)
