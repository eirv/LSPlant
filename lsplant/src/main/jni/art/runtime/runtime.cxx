module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:runtime;

import :common;
#endif

namespace lsplant::art {

namespace jni {
class JniIdManager;
}

export class Runtime {
public:
    enum class RuntimeDebugState {
        // This doesn't support any debug features / method tracing. This is the expected state
        // usually.
        kNonJavaDebuggable,
        // This supports method tracing and a restricted set of debug features (for ex: redefinition
        // isn't supported). We transition to this state when method tracing has started or when the
        // debugger was attached and transition back to NonDebuggable once the tracing has stopped /
        // the debugger agent has detached..
        kJavaDebuggable,
        // The runtime was started as a debuggable runtime. This allows us to support the extended
        // set
        // of debug features (for ex: redefinition). We never transition out of this state.
        kJavaDebuggableAtInit
    };

private:
    inline static auto instance_ = "_ZN3art7Runtime9instance_E"_sym.as<Runtime *>;

    inline static auto SetJavaDebuggable_ =
        "_ZN3art7Runtime17SetJavaDebuggableEb"_sym.as<void (Runtime::*)(bool)>;

    inline static auto SetRuntimeDebugState_ =
        "_ZN3art7Runtime20SetRuntimeDebugStateENS0_17RuntimeDebugStateE"_sym
            .as<void (Runtime::*)(RuntimeDebugState)>;

    inline static size_t debug_state_offset{};
    inline static size_t jni_id_manager_offset{};

public:
    static Runtime *Current() { return *instance_; }

    jni::JniIdManager *GetJniIdManager() {
        if (!jni_id_manager_offset) [[unlikely]] {
            return nullptr;
        }
        return *reinterpret_cast<jni::JniIdManager **>(reinterpret_cast<uintptr_t>(this) +
                                                       jni_id_manager_offset);
    }

    void SetJavaDebuggable(RuntimeDebugState value) {
        if (SetJavaDebuggable_) {
            SetJavaDebuggable_(this, value != RuntimeDebugState::kNonJavaDebuggable);
        } else if (debug_state_offset > 0) {
            *reinterpret_cast<RuntimeDebugState *>(reinterpret_cast<uintptr_t>(this) +
                                                   debug_state_offset) = value;
        }
    }

    static bool Init(JNIEnv *env, const HookHandler &handler) {
        int sdk_int = GetAndroidApiLevel();
        if (!handler(instance_) || !*instance_) [[unlikely]] {
            LOGE("Failed to find Runtime::instance_");
            return false;
        }
        if (sdk_int >= __ANDROID_API_R__) [[likely]] {
            JavaVM *vm = nullptr;
            env->GetJavaVM(&vm);
            if (vm) [[likely]] {
                auto runtime = reinterpret_cast<void **>(*instance_);
                for (size_t i = 1; 512 > i; ++i) {
                    if (runtime[i] != vm) continue;
                    if (reinterpret_cast<uintptr_t>(runtime[i - 1]) < 0x8000) continue;
                    jni_id_manager_offset = (i - 1) * kPointerSize;
                    break;
                }
            }
            if (jni_id_manager_offset) [[likely]] {
                LOGD("Runtime::jni_id_manager_ offset: %zu", jni_id_manager_offset);
            } else {
                LOGW("Failed to find Runtime::jni_id_manager_");
            }
        }
        if (sdk_int >= __ANDROID_API_O__) [[likely]] {
            if (!handler(SetRuntimeDebugState_, SetJavaDebuggable_)) [[unlikely]] {
                LOGE("Failed to find SetJavaDebuggable or SetRuntimeDebugState");
                return false;
            }
        }
        if (SetRuntimeDebugState_) [[likely]] {
            static constexpr size_t kLargeEnoughSizeForRuntime = 4096;
            std::array<uint8_t, kLargeEnoughSizeForRuntime> code{};
            static_assert(static_cast<int>(RuntimeDebugState::kJavaDebuggable) != 0);
            static_assert(static_cast<int>(RuntimeDebugState::kJavaDebuggableAtInit) != 0);
            auto *const fake_runtime = reinterpret_cast<Runtime *>(code.data());
            SetRuntimeDebugState_(fake_runtime, RuntimeDebugState::kJavaDebuggable);
            for (size_t i = 0; i < kLargeEnoughSizeForRuntime; ++i) {
                if (*reinterpret_cast<RuntimeDebugState *>(
                        reinterpret_cast<uintptr_t>(fake_runtime) + i) ==
                    RuntimeDebugState::kJavaDebuggable) {
                    LOGD("Runtime::runtime_debug_state_ offset: %zu", i);
                    debug_state_offset = i;
                    break;
                }
            }
            if (debug_state_offset == 0) [[unlikely]] {
                LOGE("Failed to find Runtime::runtime_debug_state_");
                return false;
            }
        }
        return true;
    }
};

export struct JavaDebuggableGuard {
    JavaDebuggableGuard() {
        for (;;) {
            size_t expected = 0;
            if (count.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
                Runtime::Current()->SetJavaDebuggable(
                    Runtime::RuntimeDebugState::kJavaDebuggableAtInit);
                count.fetch_add(1, std::memory_order_release);
                count.notify_all();
                break;
            }
            if (expected == 1) {
                count.wait(expected, std::memory_order_acquire);
                continue;
            }
            if (count.compare_exchange_strong(expected, expected + 1, std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
                break;
            }
        }
    }

    ~JavaDebuggableGuard() {
        for (;;) {
            size_t expected = 2;
            if (count.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
                Runtime::Current()->SetJavaDebuggable(
                    Runtime::RuntimeDebugState::kNonJavaDebuggable);
                count.fetch_sub(1, std::memory_order_release);
                count.notify_all();
                break;
            }
            if (expected == 1) {
                count.wait(expected, std::memory_order_acquire);
                continue;
            }
            if (count.compare_exchange_strong(expected, expected - 1, std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
                break;
            }
        }
    }

private:
    inline static std::atomic_size_t count{0};
    static_assert(std::atomic_size_t::is_always_lock_free, "Unsupported architecture");
    static_assert(std::is_same_v<std::atomic_size_t::value_type, size_t>,
                  "Unsupported architecture");
};
}  // namespace lsplant::art
