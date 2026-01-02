#include <android/log.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <jni.h>
#include <inttypes.h>
#include <thread>
#include <vector>
#include "zygisk.hpp"

// ================= 配置区域 =================
// 目标包名 (B服: com.bilibili.fatego / 日服: com.aniplex.fategrandorder)
static const char* TARGET_PACKAGE = "com.bilibili.fatego";

// 目标库名
static const char* TARGET_LIB = "libil2cpp.so";

// 日志标签
#define LOG_TAG "FGO_GOD"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
// ===========================================

class FgoModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // [安全锁] 1. 获取当前进程名
        const char *process_name = env->GetStringUTFChars(args->nice_name, nullptr);

        if (process_name == nullptr) return;

        // [安全锁] 2. 检查是否为 FGO 进程
        // 如果不是 FGO，告诉 Zygisk 卸载本模块，绝不注入 System Server 或其他 APP
        if (strcmp(process_name, TARGET_PACKAGE) != 0) {
            env->ReleaseStringUTFChars(args->nice_name, process_name);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGD("检测到 FGO 进程 (%s)，准备注入...", process_name);
        env->ReleaseStringUTFChars(args->nice_name, process_name);
        
        // 标记为需要注入
        is_target_process = true;
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!is_target_process) return;

        // 启动后台线程进行内存修改，避免阻塞主线程导致 ANR
        std::thread(hack_thread).detach();
    }

private:
    zygisk::Api *api;
    JNIEnv *env;
    bool is_target_process = false;

    // --- 工具函数：获取模块基址 ---
    static uintptr_t get_module_base(const char* module_name) {
        FILE *fp;
        uintptr_t addr = 0;
        char filename[32], buffer[1024];

        snprintf(filename, sizeof(filename), "/proc/self/maps");
        fp = fopen(filename, "rt");
        if (fp != nullptr) {
            while (fgets(buffer, sizeof(buffer), fp)) {
                if (strstr(buffer, module_name)) {
                    // 解析行首的十六进制地址
                    addr = (uintptr_t)strtoul(buffer, NULL, 16);
                    break;
                }
            }
            fclose(fp);
        }
        return addr;
    }

    // --- 工具函数：写入内存 ---
    static void patch_bytes(uintptr_t address, const char* bytes_hex) {
        // 简单的 hex 字符串转 byte 数组逻辑，这里为简化演示，假设输入标准 hex 串
        // 实际使用时建议引入更健壮的 Utils
        
        // 注意：这里需要处理 mprotect 权限问题
        size_t page_size = getpagesize();
        uintptr_t page_start = address & (~(page_size - 1));
        
        // 修改内存权限为可读可写可执行
        if (mprotect((void*)page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            LOGE("mprotect 失败: %s", strerror(errno));
            return;
        }

        // 这里仅做演示，写入 00 00 A0 E3 (MOV R0, #0)
        // 实际代码请配合 HexStringToBytes 函数使用
        // *(uint32_t*)address = 0xE3A00000; 

        LOGD("内存写入成功: %lx", address);
        
        // 恢复权限（可选，为了隐蔽性）
        mprotect((void*)page_start, page_size, PROT_READ | PROT_EXEC);
    }

    // --- 核心线程 ---
    static void hack_thread() {
        LOGD("外挂线程启动，等待 %s 加载...", TARGET_LIB);

        uintptr_t base_addr = 0;
        int max_attempts = 60; // 最多等待 60 秒

        // 循环等待库加载
        while (base_addr == 0 && max_attempts > 0) {
            base_addr = get_module_base(TARGET_LIB);
            if (base_addr == 0) {
                sleep(1);
                max_attempts--;
            }
        }

        if (base_addr == 0) {
            LOGE("超时！未找到 %s，可能是 64位/32位 版本不匹配或游戏未启动。", TARGET_LIB);
            return;
        }

        LOGD("捕获基址成功: 0x%" PRIxPTR, base_addr);

        // ==========================================
        //在此处填入你的 Offset 修改逻辑
        // 示例：修改攻击力函数 (假设 offset 是 0x123456)
        // patch_bytes(base_addr + 0x123456, "0000A0E31EFF2FE1"); 
        // ==========================================
        
        LOGD("Patch 完成！尽情享受吧。");
    }
};

REGISTER_ZYGISK_MODULE(FgoModule)
