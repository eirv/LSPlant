module;

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:unsafe;

import :common;
import :reflection;
#endif

export namespace lsplant::art::mirror {

class Unsafe {
    explicit Unsafe(JNIEnv *env) : env_{env}, unsafe_{the_unsafe_ref_.get(env).release()} {}

public:
    operator bool() const { return unsafe_ != nullptr; }

    [[nodiscard]] void *GetObjectAddress(jobject obj) const {
        if (!unsafe_get_int_method) [[unlikely]] {
            return nullptr;
        }
        auto object_array = JNI_NewObjectArray(env_, 1, object_class_ref.get(env_), obj);
        auto addr = JNI_CallIntMethod<uintptr_t>(env_, unsafe_, unsafe_get_int_method, object_array,
                                                 static_cast<jlong>(object_array_base_offset_));
        return reinterpret_cast<void *>(addr);
    }

    jobject NewLocalRef(void *obj) const {
        if (!unsafe_put_int_method) [[unlikely]] {
            return nullptr;
        }
        auto object_array = JNI_NewObjectArray(env_, 1, object_class_ref.get(env_), nullptr);
        JNI_CallVoidMethod(env_, unsafe_, unsafe_put_int_method, object_array,
                           static_cast<jlong>(object_array_base_offset_),
                           static_cast<jint>(reinterpret_cast<intptr_t>(obj)));
        return object_array[0].release();
    }

    [[nodiscard]] uint32_t ObjectFieldOffset(jobject field) const {
        if (!object_field_offset_method_) {
            return 0;
        }
        return JNI_CallLongMethod<uint32_t>(env_, unsafe_, object_field_offset_method_, field);
    }

    ~Unsafe() { env_->DeleteLocalRef(unsafe_); }

    static bool HasObjectFieldOffset() { return object_field_offset_method_ != nullptr; }

    static jint GetObjectArrayBaseOffset() { return object_array_base_offset_; }

    static Unsafe GetUnsafe(JNIEnv *env) { return Unsafe{env}; }

    static bool Init(JNIEnv *env) {
        auto unsafe_class = JNI_FindClass(env, "sun/misc/Unsafe");
        if (!unsafe_class) [[unlikely]] {
            LOGW("Failed to find Unsafe");
            return true;
        }

        ScopedLocalRef<jobject> the_unsafe{env, nullptr};
        if (auto the_unsafe_field = JNI_GetStaticFieldID(env, unsafe_class, "theUnsafe",
                                                         "Lsun/misc/Unsafe;")) [[likely]] {
            the_unsafe = JNI_GetStaticObjectField(env, unsafe_class, the_unsafe_field);
        }
        if (!the_unsafe) [[unlikely]] {
            the_unsafe = JNI_AllocObject(env, unsafe_class);
        }
        the_unsafe_ref_.set(env, the_unsafe.get());

        object_field_offset_method_ =
            JNI_GetMethodID(env, unsafe_class, "objectFieldOffset", "(Ljava/lang/reflect/Field;)J");

        if (auto array_base_offset_method = JNI_GetMethodID(env, unsafe_class, "arrayBaseOffset",
                                                            "(Ljava/lang/Class;)I")) [[likely]] {
            auto object_array_class = JNI_FindClass(env, "[Ljava/lang/Object;");
            object_array_base_offset_ = JNI_CallNonvirtualIntMethod(
                env, the_unsafe, unsafe_class, array_base_offset_method, object_array_class);
            unsafe_get_int_method =
                JNI_GetMethodID(env, unsafe_class, "getInt", "(Ljava/lang/Object;J)I");
            unsafe_put_int_method =
                JNI_GetMethodID(env, unsafe_class, "putInt", "(Ljava/lang/Object;JI)V");
        }

        return true;
    }

private:
    inline static GlobalRef the_unsafe_ref_{};
    inline static jmethodID object_field_offset_method_{};
    inline static jint object_array_base_offset_{};

    JNIEnv *env_;
    jobject unsafe_;
};

}  // namespace lsplant::art::mirror
