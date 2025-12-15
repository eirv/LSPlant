module;

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:thread;

import :common;
import :unsafe;
import :handle;
#endif

namespace lsplant::art {
export class Thread {
    inline static auto CurrentFromGdb_ = "_ZN3art6Thread14CurrentFromGdbEv"_sym.as<Thread *()>;

    inline static auto Current_ = "_ZN3art6Thread7CurrentEv"_sym.as<Thread *()>;

    inline static auto JniDecodeReferenceResult_ =
        "_ZN3art24JniDecodeReferenceResultEP8_jobjectPNS_6ThreadE"_sym
            .as<void *(jobject result, Thread *self)>;

    inline static auto DecodeGlobalJObject_ =
        "_ZNK3art6Thread19DecodeGlobalJObjectEP8_jobject"_sym.as<void *(Thread::*)(jobject)>;

    inline static auto DecodeJObject_ =
        "_ZNK3art6Thread13DecodeJObjectEP8_jobject"_sym.as<void *(Thread::*)(jobject)>;

    inline static auto AddLocalReference_ =
        "_ZN3art9JNIEnvExt17AddLocalReferenceIP8_jobjectEET_NS_6ObjPtrINS_6mirror6ObjectEEE"_sym
            .as<jobject(JNIEnv *, ObjPtr<void> obj)>;

public:
    void *DecodeJObject(JNIEnv *env, jobject obj) {
        if (!obj) [[unlikely]] {
            return nullptr;
        }
        if (JniDecodeReferenceResult_) [[likely]] {
            return JniDecodeReferenceResult_(obj, this);
        }
        if (DecodeGlobalJObject_) {
            if (auto weak = env->NewWeakGlobalRef(obj)) {
                auto result = DecodeGlobalJObject_(this, weak);
                env->DeleteWeakGlobalRef(weak);
                return result;
            }
        }
        if constexpr (!is_arch_v<Arch::kX86>) {
            if (DecodeJObject_) {
                return DecodeJObject_(this, obj);
            }
        }
        if (auto unsafe = mirror::Unsafe::GetUnsafe(env)) [[likely]] {
            return unsafe.GetObjectAddress(obj);
        }
        return nullptr;
    }

    jobject EncodeJObject(JNIEnv *env, void *obj) {
        if (AddLocalReference_) [[likely]] {
            return AddLocalReference_(env, obj);
        }
        if (auto unsafe = mirror::Unsafe::GetUnsafe(env)) [[likely]] {
            return unsafe.NewLocalRef(obj);
        }
        return nullptr;
    }

    static void **GetTLS() {
        void **tls;
        if constexpr (is_arch_v<Arch::kArm64>) {
            asm volatile("mrs %0, tpidr_el0" : "=r"(tls));
        } else if constexpr (is_arch_v<Arch::kArm>) {
            asm volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
        } else if constexpr (is_arch_v<Arch::kX86>) {
            asm volatile("movl %%gs:0, %0" : "=r"(tls));
        } else if constexpr (is_arch_v<Arch::kX64>) {
            asm volatile("mov %%fs:0, %0" : "=r"(tls));
        } else if constexpr (is_arch_v<Arch::kRiscv64>) {
            asm volatile("mv %0, tp" : "=r"(tls));
        } else {
            return nullptr;
        }
        return tls;
    }

    static Thread *Current() {
        if (CurrentFromGdb_) [[likely]] {
            return CurrentFromGdb_();
        }
        if (Current_) [[unlikely]] {
            return Current_();
        }
        return CurrentFromTLS();
    }

    static bool Init(const HookHandler &handler) {
        if (!handler(CurrentFromGdb_)) [[unlikely]] {
            if (GetAndroidApiLevel() >= __ANDROID_API_N__) [[likely]] {
                LOGW("Failed to find Thread::CurrentFromGdb; use TLS instead.");
            } else if (!handler(Current_)) [[unlikely]] {
                LOGW("Failed to find Thread::CurrentFromGdb or Thread::Current");
                return false;
            }
        }
        if (!handler(JniDecodeReferenceResult_, DecodeGlobalJObject_)) [[unlikely]] {
            if constexpr (!is_arch_v<Arch::kX86>) {
                handler(DecodeJObject_);
            }
        }
        return true;
    }

private:
    constexpr static int kSlotArtThreadSelf = is_arch_v<Arch::kRiscv64> ? -1 : 7;

    static Thread *CurrentFromTLS() {
        if constexpr (is_arch_v<Arch::kRiscv64>) {
            Thread *thread;
            asm volatile("ld %0, (%1)(tp)" : "=r"(thread) : "i"(kSlotArtThreadSelf * kPointerSize));
            return thread;
        } else {
            return static_cast<Thread *>(GetTLS()[kSlotArtThreadSelf]);
        }
    }
};
}  // namespace lsplant::art
