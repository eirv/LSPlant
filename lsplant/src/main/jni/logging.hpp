#pragma once

#include <android/log.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <source_location>

#ifndef LOG_TAG
#define LOG_TAG "LSPlant"
#endif

#ifdef LOG_DISABLED
#define LOGD(...) 0
#define LOGV(...) 0
#define LOGI(...) 0
#define LOGW(...) 0
#define LOGE(...) 0
#define PLOGE(...) 0
#define TIMED_FUNCTION() 0
#else
#define LOG(prio, fmt, ...)                                                                        \
    (::lsplant::IsForamttable(fmt)                                                                 \
         ? __android_log_print(ANDROID_LOG_##prio, LOG_TAG, fmt, ##__VA_ARGS__)                    \
         : __android_log_write(ANDROID_LOG_##prio, LOG_TAG, fmt))
#ifndef NDEBUG
#define LOGD(fmt, ...)                                                                             \
    LOG(DEBUG,                                                                                     \
        "%s:%d"                                                                                    \
        ": " fmt,                                                                                  \
        __FILE_NAME__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)
#define LOGV(fmt, ...)                                                                             \
    LOG(VERBOSE,                                                                                   \
        "%s:%d"                                                                                    \
        ": " fmt,                                                                                  \
        __FILE_NAME__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)
#define TIMED_FUNCTION() ::lsplant::FunctionTimer timer##__LINE__(std::source_location::current())
#else
#define LOGD(...) 0
#define LOGV(...) 0
#define TIMED_FUNCTION() 0
#endif
#define LOGI(...) LOG(INFO, __VA_ARGS__)
#define LOGW(...) LOG(WARN, __VA_ARGS__)
#define LOGE(...) LOG(ERROR, __VA_ARGS__)
#define LOGF(...) LOG(FATAL, __VA_ARGS__)
#define PLOGE(fmt, args...) LOGE(fmt " failed with %d: %s", ##args, errno, std::strerror(errno))
#endif

namespace lsplant {
template <size_t N>
[[maybe_unused]] consteval bool IsForamttable(const char (&fmt)[N]) {
    for (size_t i = 0; N > i; ++i) {
        if (fmt[i] == '%') return true;
    }
    return false;
}

class FunctionTimer {
public:
    explicit FunctionTimer(std::source_location source)
        : source_(source), start_time_(std::chrono::high_resolution_clock::now()) {}

    ~FunctionTimer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
        if (duration.count() < 1'000) {
            LOGI("[%s]: took %lldμs", source_.function_name(), duration.count());
        } else if (duration.count() < 1'000'000) {
            LOGI("[%s]: took %.3lfms", source_.function_name(), duration.count() / 1'000.0);
        } else {
            LOGI("[%s]: took %.3lfs", source_.function_name(), duration.count() / 1'000'000.0);
        }
    }

    FunctionTimer(const FunctionTimer &) = delete;
    FunctionTimer &operator=(const FunctionTimer &) = delete;

private:
    std::source_location source_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
};
}  // namespace lsplant
