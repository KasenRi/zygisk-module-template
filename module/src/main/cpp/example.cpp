#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <vector>

// 必须确认你的项目包含了 Dobby 库，否则这里会报错
// 如果报错找不到 dobby.h，你需要去下载 Dobby 并配置 CMakeLists.txt
#include "dobby.h" 

// LOG定义，方便你在 Logcat 里看调试信息
#define TAG "FGO_MOD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ==========================================
// 核心偏移量 (来自 dump.cs)
// ==========================================
// getBaseATK: RVA 0x24be874
uintptr_t OFFSET_ATK = 0x24be874;

// get_hp: RVA 0x24a46f8
uintptr_t OFFSET_HP  = 0x24a46f8;
// ==========================================

// 旧函数指针，用来保存原函数（如果想恢复或者调用原逻辑）
int (*old_getBaseATK)(void *instance);
int (*old_get_hp)(void *instance);

// 新函数：攻击力 Hook
int new_getBaseATK(void *instance) {
    // 直接返回 999999，一刀秒杀
    // LOGD("getBaseATK called - Cheating!"); 
    return 999999;
}

// 新函数：血量 Hook
int new_get_hp(void *instance) {
    // 锁定血量 999999，无敌模式
    return 999999;
}

// 辅助函数：读取 /proc/self/maps 获取 libil2cpp.so 的基地址
void *get_base_address(const char *name) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return nullptr;

    char line[512];
    void *addr = nullptr;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, name)) {
            // 解析行首的十六进制地址
            sscanf(line, "%lx", (long *)&addr);
            break;
        }
    }
    fclose(fp);
    return addr;
}

// 作弊主线程
void *hack_thread(void *arg) {
    LOGD("Hack thread started. Waiting for libil2cpp.so...");

    // 循环等待游戏加载 libil2cpp.so
    void *il2cpp_base = nullptr;
    while (il2cpp_base == nullptr) {
        il2cpp_base = get_base_address("libil2cpp.so");
        sleep(1); // 每秒检查一次
    }

    LOGD("Found libil2cpp.so at: %p", il2cpp_base);
    LOGD("Applying Hooks...");

    // 计算绝对地址
    void *addr_atk = (void *)((uintptr_t)il2cpp_base + OFFSET_ATK);
    void *addr_hp  = (void *)((uintptr_t)il2cpp_base + OFFSET_HP);

    // 执行 Hook (Dobby)
    // 参数：目标地址，新函数，旧函数保存位置
    DobbyHook(addr_atk, (void *)new_getBaseATK, (void **)&old_getBaseATK);
    DobbyHook(addr_hp,  (void *)new_get_hp,     (void **)&old_get_hp);

    LOGD("Hooks Applied! Enjoy 999999 ATK/HP.");
    return nullptr;
}

// JNI 入口 (Zygisk 注入后会自动调用这个)
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    LOGD("Module Loaded via JNI_OnLoad");

    // 创建一个新线程去执行 Hook，避免阻塞主线程导致游戏卡死
    pthread_t pt;
    pthread_create(&pt, nullptr, hack_thread, nullptr);

    return JNI_VERSION_1_6;
}
