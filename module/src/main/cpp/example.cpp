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
const char *TARGET_PACKAGE = "com.bilibili.fatego";
// =========================================

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// 通用内存 Patch 函数
void patch_bytes(uintptr_t address, const unsigned char* code, size_t len) {
    long page_size = sysconf(_SC_PAGE_SIZE);
    void *page_start = (void *)(address & ~(page_size - 1));

    if (mprotect(page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1) {
        LOGD("Error: mprotect 失败，无法修改内存权限!");
        return;
    }

    memcpy((void *)address, code, len);
    __builtin___clear_cache((char *)address, (char *)address + len);
    
    LOGD("Patch 成功: 地址 %p, 长度 %zu", (void*)address, len);
}

// 核心外挂线程
void *hack_thread(void *arg) {
    LOGD("=== FGO 外挂线程启动 ===");
    LOGD("正在等待 libil2cpp.so 加载...");

    void *base_addr = nullptr;

    while (base_addr == nullptr) {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp) {
            char line[512];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "libil2cpp.so")) {
                    base_addr = (void *)strtoul(line, nullptr, 16);
                    break;
                }
            }
            fclose(fp);
        }
        usleep(500000);
    }

    LOGD("捕获 libil2cpp.so 基址: %p", base_addr);

    // =======================================================
    // 功能 1: 伤害修改 (One Hit Kill)
    // Offset: 0x24bbfb8
    // =======================================================
    
    uintptr_t offset_damage = 0x24bbfb8; 
    uintptr_t target_addr = (uintptr_t)base_addr + offset_damage;

    // Payload: return 999999;
    unsigned char damage_payload[] = {
        0xE0, 0x47, 0x88, 0x52, // MOVZ W0, #0x423F
        0xE0, 0x01, 0xA0, 0x72, // MOVK W0, #0xF, LSL #16
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };

    LOGD("正在应用 [秒杀] Patch...");
    patch_bytes(target_addr, damage_payload, sizeof(damage_payload));

    LOGD("=== 所有 Patch 执行完毕，请进战斗测试 ===");
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
        
        if (process && strcmp(process, TARGET_PACKAGE) == 0) {
            // [修复点] 这里修正了枚举名称，删除了 DENY 和 LIST 之间的下划线
            api->setOption(zygisk::Option::FORCE_DENYLIST_UNMOUNT);
            LOGD("发现目标进程 FGO，准备注入...");
        }
        
        env->ReleaseStringUTFChars(args->nice_name, process);
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        
        if (process && strcmp(process, TARGET_PACKAGE) == 0) {
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

REGISTER_ZYGISK_MODULE(FgoModule)
