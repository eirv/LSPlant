module;

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:jni_id_manager;

import :common;
import :handle;
#endif

namespace lsplant::art {

class ArtMethod;

namespace jni {
export class JniIdManager {
private:
    inline static auto EncodeGenericId_ =
        "_ZN3art3jni12JniIdManager15EncodeGenericIdINS_9ArtMethodEEEmNS_16ReflectiveHandleIT_EE"_sym
            .hook
            ->*[]<MemBackup auto backup>(JniIdManager *thiz,
                                         ReflectiveHandle<ArtMethod> method) static -> uintptr_t {
        if (auto target = IsBackup(method.Get()); target) {
            // LOGD("get generic id for %s", method.Get()->PrettyMethod().c_str());
            method.Set(target);
        }
        return backup(thiz, method);
    };

    inline static auto DecodeMethodId_ =
        "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"_sym
            .as<ArtMethod *(JniIdManager::*)(jmethodID method)>;

public:
    ArtMethod *DecodeMethodId(jmethodID method) {
        if (DecodeMethodId_) [[likely]] {
            return DecodeMethodId_(this, method);
        }
        return nullptr;
    }

    template <typename T>
        requires(std::is_same_v<T, jmethodID> || std::is_same_v<T, jfieldID>)
    static constexpr bool IsIndexId(T val) {
        return val == nullptr || (reinterpret_cast<uintptr_t>(val) & (kPointerSize - 1)) != 0;
    }

    static bool Init(JNIEnv *env, const HookHandler &handler) {
        int sdk_int = GetAndroidApiLevel();
        if (sdk_int >= __ANDROID_API_R__) {
            handler(DecodeMethodId_);
            if (IsJavaDebuggable(env) && !handler(EncodeGenericId_)) {
                LOGW("Failed to hook EncodeGenericId, attaching debugger may crash the process");
            }
        }
        return true;
    }
};
}  // namespace jni

}  // namespace lsplant::art
