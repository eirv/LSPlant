module;

#include <parallel_hashmap/phmap.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:stack;

import :common;
import :clazz;
import :art_method;
import :thread;
#endif

export namespace lsplant::art {

class StackVisitor {
    inline static auto GetMethod_ =
        "_ZNK3art12StackVisitor9GetMethodEv"_sym.as<ArtMethod *(StackVisitor::*)()>;

    inline static auto SetMethod_ = "_ZN3art12StackVisitor9SetMethodEPNS_9ArtMethodE"_sym
                                        .as<void (StackVisitor::*)(ArtMethod *)>;

    constexpr static uint32_t kNativeMethodDexPc = -2;

    inline static phmap::parallel_flat_hash_set<ArtMethod *> hidden_methods_;
    inline static phmap::parallel_flat_hash_map<Thread *, std::pair<ArtMethod *, ArtMethod *>>
        current_method_;

    static bool HandleVisitFrame(StackVisitor *visitor, bool (*backup)(StackVisitor *)) {
        if (hidden_methods_.empty()) [[unlikely]] {
            return backup(visitor);
        }

        auto method = visitor->GetMethod();
        if (!method) [[unlikely]] {
            return backup(visitor);
        }

        if (hidden_methods_.contains(method)) [[unlikely]] {
            return true;
        } else if (!SetMethod_) [[unlikely]] {
            return backup(visitor);
        }

        if (auto target = IsBackup(method)) [[unlikely]] {
            auto self = Thread::Current();
            current_method_[self] = {target, method};

            auto result = backup(visitor);

            current_method_.erase(self);
            return result;
        }

        return backup(visitor);
    }

    static uint32_t HandleGetDexPc(StackVisitor *thiz, bool abort_on_failure,
                                   uint32_t (*backup)(StackVisitor *, bool)) {
        if (current_method_.empty()) [[likely]] {
            return backup(thiz, abort_on_failure);
        }

        const auto &found = current_method_.find(Thread::Current());
        if (found == current_method_.end()) [[unlikely]] {
            return backup(thiz, abort_on_failure);
        }

        auto [target, backup_method] = found->second;
        if (backup_method != thiz->GetMethod()) [[unlikely]] {
            return backup(thiz, abort_on_failure);
        }

        if (backup_method->IsNative()) {
            return kNativeMethodDexPc;
        }

        auto dex_pc = backup(thiz, false);
        thiz->SetMethod(target);

        return dex_pc;
    }

    inline static auto GetDexPc_ =
        "_ZNK3art12StackVisitor8GetDexPcEb"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz, bool abort_on_failure) static -> uint32_t {
        return HandleGetDexPc(thiz, abort_on_failure, backup);
    };

    inline static auto VisitFrameFetchStackTrace_ =
        "_ZN3art22FetchStackTraceVisitor10VisitFrameEv"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto VisitFrameBuildInternalStackTrace_ =
        "_ZN3art30BuildInternalStackTraceVisitor10VisitFrameEv"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto VisitFrameBuildInternalStackTraceTransactionActive_ =
        "_ZN3art30BuildInternalStackTraceVisitorILb1EE10VisitFrameEv"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto VisitFrameBuildInternalStackTraceTransactionInactive_ =
        "_ZN3art30BuildInternalStackTraceVisitorILb0EE10VisitFrameEv"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto VisitFrameMonitorObjects_ =
        "_ZN3art26MonitorObjectsStackVisitor10VisitFrameEv"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto VisitFrameStackDump_ =
        "_ZN3art16StackDumpVisitor10VisitFrameEv"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto VisitFrameNthCaller_ =
        "_ZN3art16NthCallerVisitor10VisitFrameEv"_sym.hook->*
        []<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto VisitFrameClosestUserClassLoader_ =
        "_ZZN3artL33VMStack_getClosestUserClassLoaderEP7_JNIEnvP7_jclassEN29ClosestUserClassLoaderVisitor10VisitFrameEv"_sym
            .hook
            ->*[]<MemBackup auto backup>(StackVisitor *thiz) static -> bool {
        return HandleVisitFrame(thiz, backup);
    };

    inline static auto NativeFillInStackTrace_ =
        ""_sym.hook->*
        []<Backup auto backup>(JNIEnv *env, jclass throwable_class) static -> jobjectArray {
        auto backtrace = WrapScope(env, backup(env, throwable_class));
        if (!backtrace || backtrace.size() <= 1) [[unlikely]] {
            return backtrace.release();
        }

        auto frame_count = backtrace.size() - 1;
        auto ptr_array = JNI_Cast<jpointerArray>(backtrace[0]);
        if (ptr_array.size() / 2 != frame_count) {
            return backtrace.release();
        }

        JLocalObjectList new_backtrace{};
        new_backtrace.reserve(env, backtrace.size());

        // reserved
        new_backtrace.add(env, ptr_array.get());

        std::vector<std::pair<ArtMethod *, intptr_t>> new_methods;
        for (size_t i = 0; frame_count > i; ++i) {
            auto method = reinterpret_cast<ArtMethod *>(ptr_array[i]);
            auto dex_pc = static_cast<intptr_t>(ptr_array[frame_count + i]);
            if (hidden_methods_.contains(method)) [[unlikely]] {
                continue;
            } else if (auto target = IsBackup(method)) [[unlikely]] {
                if (method->IsNative()) [[unlikely]] {
                    dex_pc = kNativeMethodDexPc;
                }
                method = target;
            }
            new_methods.emplace_back(std::pair{method, dex_pc});
            new_backtrace.add(env, backtrace[i + 1].get());
        }

        auto new_ptr_array = JNI_NewArray<jpointer>(env, new_methods.size() * 2);
        for (size_t i = 0; new_methods.size() > i; ++i) {
            auto [method, dex_pc] = new_methods[i];
            new_ptr_array[i] = reinterpret_cast<intptr_t>(method);
            new_ptr_array[new_methods.size() + i] = dex_pc;
        }
        new_ptr_array.commit();

        new_backtrace.update(env, 0, new_ptr_array.get());
        new_backtrace.finalize(env);

        return new_backtrace.get_array();
    };

public:
    static bool HideMethod(ArtMethod *method) {
        if (method->IsAbstract()) [[unlikely]] {
            return false;
        }
        return hidden_methods_.insert(method).second;
    }

    static size_t HideClass(JNIEnv *env, jclass clazz) {
        size_t hidden = 0;
        mirror::Class::VisitMethods(env, clazz, [&](auto method) {
            if (method->IsAbstract()) return false;
            if (hidden_methods_.insert(method).second) [[likely]] {
                ++hidden;
            }
            return false;
        });
        return hidden;
    }

    static size_t ShowClass(JNIEnv *env, jclass clazz) {
        size_t restored = 0;
        mirror::Class::VisitMethods(env, clazz, [&](auto method) {
            if (method->IsAbstract()) return false;
            if (hidden_methods_.erase(method) > 0) {
                ++restored;
            }
            return false;
        });
        return restored;
    }

    static bool Init(JNIEnv *env, const HookHandler &handler) {
        auto sdk_int = GetAndroidApiLevel();

        if (sdk_int >= __ANDROID_API_N__) [[likely]] {
            if (!handler(GetMethod_)) [[unlikely]] {
                LOGW(
                    "Failed to find StackVisitor::GetMethod, use hard-coded StackVisitor structure instead");
            }
            if (sdk_int >= __ANDROID_API_O__ && handler(SetMethod_) && !handler(GetDexPc_))
                [[unlikely]] {
                SetMethod_ = nullptr;
            }
        }

        // java.lang.Throwable->getStackTrace
        // java.lang.Thread->getStackTrace
        // java.lang.StackWalker->Walk
        auto hook_stack_trace = [=] {
            if (sdk_int >= __ANDROID_API_O__ && handler(VisitFrameFetchStackTrace_)) [[likely]] {
                return true;
            }
            if (sdk_int >= __ANDROID_API_S__) [[likely]] {
                return handler(VisitFrameBuildInternalStackTrace_);
            } else {
                auto active = handler(VisitFrameBuildInternalStackTraceTransactionActive_);
                auto inactive = handler(VisitFrameBuildInternalStackTraceTransactionInactive_);
                return active || inactive || handler(VisitFrameBuildInternalStackTrace_);
            }
        };
        if (!hook_stack_trace()) [[unlikely]] {
            if (sdk_int >= __ANDROID_API_M__) [[likely]] {
                auto throwable_class = JNI_FindClass(env, "java/lang/Throwable");
                auto method = ArtMethod::FindStaticMethod(
                    env, throwable_class.get(), "nativeFillInStackTrace", "()Ljava/lang/Object;");
                if (!method) [[unlikely]] {
                    LOGW(
                        "Unable to hide methods in stack trace; Throwable.nativeFillInStackTrace not found");
                } else if (!method->NativeHook(NativeFillInStackTrace_)) [[unlikely]] {
                    LOGW(
                        "Unable to hide methods in stack trace; Throwable.nativeFillInStackTrace hook failed");
                }
            } else {
                LOGW("Unable to hide methods in stack trace; VisitFrame not found");
            }
            return true;
        }

        // art::Thread::DumpJavaStack
        if (sdk_int >= __ANDROID_API_P__) [[likely]] {
            handler(VisitFrameMonitorObjects_);
        } else {
            handler(VisitFrameStackDump_);
        }

        // dalvik.system.VMStack->getStackClass2
        // dalvik.system.VMStack->getCallingClassLoader
        handler(VisitFrameNthCaller_);

        // dalvik.system.VMStack->getClosestUserClassLoader
        handler(VisitFrameClosestUserClassLoader_, true);

        return true;
    }

private:
    enum class StackWalkKind {
        kIncludeInlinedFrames [[maybe_unused]],
        kSkipInlinedFrames [[maybe_unused]],
    };

    class ShadowFrame {
    public:
        [[nodiscard]] ArtMethod *GetMethod() const { return method_; }

    private:
        [[maybe_unused]] ShadowFrame *link_{nullptr};
        ArtMethod *method_{nullptr};
    };

    [[nodiscard]] ArtMethod *GetMethod() {
        if (GetMethod_) [[likely]] {
            return GetMethod_(this);
        } else if (cur_shadow_frame_ != nullptr) {
            return cur_shadow_frame_->GetMethod();
        } else if (cur_quick_frame_ != nullptr) {
            return *cur_quick_frame_;
        } else {
            return nullptr;
        }
    }

    void SetMethod(ArtMethod *method) {
        if (SetMethod_) [[likely]] {
            SetMethod_(this, method);
        }
    }

    // Only used on Android 5~6
    // Android 7 and above are also OK
    [[maybe_unused]] void **vtable_{};
    [[maybe_unused]] Thread *const thread_{};
    [[maybe_unused]] const StackWalkKind walk_kind_{};
    ShadowFrame *cur_shadow_frame_{};
    ArtMethod **cur_quick_frame_{};
};

}  // namespace lsplant::art
