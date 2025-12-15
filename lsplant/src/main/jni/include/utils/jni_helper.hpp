#pragma once

#include <android/log.h>
#include <jni.h>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "type_traits.hpp"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-partial-specialization"
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define LSPLANT_DISALLOW_COPY_AND_ASSIGN(TypeName)                                                 \
    TypeName(const TypeName &) = delete;                                                           \
    void operator=(const TypeName &) = delete

namespace lsplant {

using jpointer = std::conditional_t<is_arch_v<Arch::kLP64>, jlong, jint>;
using jpointerArray = std::conditional_t<is_arch_v<Arch::kLP64>, jlongArray, jintArray>;

template <typename T>
concept JObject = std::is_base_of_v<std::remove_pointer_t<_jobject>, std::remove_pointer_t<T>>;

template <JObject T>
class ScopedLocalRef {
public:
    using BaseType [[maybe_unused]] = T;

    ScopedLocalRef(JNIEnv *env, T local_ref) : env_(env), local_ref_(nullptr) { reset(local_ref); }

    ScopedLocalRef(ScopedLocalRef &&s) noexcept : ScopedLocalRef(s.env_, s.release()) {}

    template <JObject U>
    ScopedLocalRef(ScopedLocalRef<U> &&s) noexcept : ScopedLocalRef(s.env_, (T)s.release()) {}

    explicit ScopedLocalRef(JNIEnv *env) noexcept : ScopedLocalRef(env, T{nullptr}) {}

    ~ScopedLocalRef() { reset(); }

    void reset(T ptr = nullptr) {
        if (ptr != local_ref_) {
            if (local_ref_ != nullptr) {
                env_->DeleteLocalRef(local_ref_);
            }
            local_ref_ = ptr;
        }
    }

    [[nodiscard]] T release() {
        T localRef = local_ref_;
        local_ref_ = nullptr;
        return localRef;
    }

    T get() const { return local_ref_; }

    ScopedLocalRef<T> clone() const {
        return ScopedLocalRef<T>(env_, (T)env_->NewLocalRef(local_ref_));
    }

    ScopedLocalRef &operator=(ScopedLocalRef &&s) noexcept {
        reset(s.release());
        env_ = s.env_;
        return *this;
    }

    operator bool() const { return local_ref_; }

    template <JObject U>
    friend class ScopedLocalRef;

    friend class JUTFString;
    friend class JString;

    template <typename U>
    friend class JDirectBuffer;

private:
    JNIEnv *env_;
    T local_ref_;
    LSPLANT_DISALLOW_COPY_AND_ASSIGN(ScopedLocalRef);
};

class JObjectArrayElement;

template <typename T>
class JDirectBuffer;

template <typename T>
concept JArray = std::is_base_of_v<std::remove_pointer_t<_jarray>, std::remove_pointer_t<T>>;

template <JArray T>
class ScopedLocalRef<T>;

class JNIScopeFrame {
public:
    JNIScopeFrame(JNIEnv *env, jint capacity) : env_(env) { env_->PushLocalFrame(capacity); }

    template <JObject Object>
    Object pop(Object obj) {
        if (env_) [[likely]] {
            auto result = reinterpret_cast<Object>(env_->PopLocalFrame(obj));
            env_ = nullptr;
            return result;
        }
        return nullptr;
    }

    ~JNIScopeFrame() {
        if (env_) env_->PopLocalFrame(nullptr);
    }

private:
    JNIEnv *env_;

    LSPLANT_DISALLOW_COPY_AND_ASSIGN(JNIScopeFrame);
};

class JNIMonitor {
    JNIEnv *env_;
    jobject obj_;

    LSPLANT_DISALLOW_COPY_AND_ASSIGN(JNIMonitor);

public:
    JNIMonitor(JNIEnv *env, jobject obj) : env_(env), obj_(obj) { env_->MonitorEnter(obj_); }

    ~JNIMonitor() { env_->MonitorExit(obj_); }
};

template <typename T, typename U>
concept ScopeOrRaw =
    std::is_convertible_v<T, U> ||
    (is_instance_v<std::decay_t<T>, ScopedLocalRef> &&
     std::is_convertible_v<typename std::decay_t<T>::BaseType, U>) ||
    (std::is_same_v<std::decay_t<T>, JObjectArrayElement> && std::is_convertible_v<jobject, U>);

template <typename T>
concept ScopeOrClass = ScopeOrRaw<T, jclass>;

template <typename T>
concept ScopeOrObject = ScopeOrRaw<T, jobject>;

template <typename T>
concept ScopeOrJCompatible = ScopeOrObject<T> || (std::is_integral_v<T> && sizeof(T) <= 8);

template <typename T>
concept ScopeOrJCompatiblePointer = ScopeOrJCompatible<T> || std::is_pointer_v<T>;

template <typename T>
[[maybe_unused]] inline auto UnwrapScope(T &&x) {
    if constexpr (std::is_same_v<std::decay_t<T>, std::string_view>)
        return x.data();
    else if constexpr (is_instance_v<std::decay_t<T>, ScopedLocalRef>)
        return x.get();
    else if constexpr (std::is_same_v<std::decay_t<T>, JObjectArrayElement>)
        return x.get();
    else if constexpr (is_instance_v<std::decay_t<T>, JDirectBuffer>)
        return x.get();
    else if constexpr (std::is_same_v<std::decay_t<T>, bool>)
        return x ? JNI_TRUE : JNI_FALSE;
    else
        return std::forward<T>(x);
}

template <typename T>
[[maybe_unused]] inline auto WrapScope(JNIEnv *env, T &&x) {
    if constexpr (std::is_convertible_v<T, _jobject *>) {
        return ScopedLocalRef(env, std::forward<T>(x));
    } else
        return x;
}

template <typename... T, size_t... I>
[[maybe_unused]] inline auto WrapScope(JNIEnv *env, std::tuple<T...> &&x,
                                       std::index_sequence<I...>) {
    return std::make_tuple(WrapScope(env, std::forward<T>(std::get<I>(x)))...);
}

template <typename... T>
[[maybe_unused]] inline auto WrapScope(JNIEnv *env, std::tuple<T...> &&x) {
    return WrapScope(env, std::forward<std::tuple<T...>>(x),
                     std::make_index_sequence<sizeof...(T)>());
}

inline auto JNI_NewStringUTF(JNIEnv *env, std::string_view sv) {
    return ScopedLocalRef(env, env->NewStringUTF(sv.data()));
}

inline auto JNI_NewString(JNIEnv *env, std::u16string_view sv) {
    return ScopedLocalRef(env, env->NewString(reinterpret_cast<const jchar *>(sv.data()),
                                              static_cast<jsize>(sv.size())));
}

template <typename T>
    requires(std::is_same_v<T, char> || std::is_same_v<T, char16_t>)
class JBasicString {
protected:
    JBasicString(JNIEnv *env, jstring jstr, const T *data = nullptr)
        : env_(env), jstr_(jstr), data_(data) {}

    JNIEnv *env_;
    jstring jstr_;
    const T *data_;

public:
    operator const T *() const { return data_; }

    operator const bool() const { return data_ != nullptr; }

    auto get() const { return data_; }

private:
    JBasicString(const JBasicString &) = delete;

    JBasicString &operator=(const JBasicString &) = delete;
};

class JUTFString : public JBasicString<char> {
public:
    JUTFString(const ScopedLocalRef<jstring> &jstr) : JUTFString(jstr.env_, jstr.local_ref_) {}

    JUTFString(JNIEnv *env, jstring jstr, std::optional<std::string_view> default_str = {})
        : JBasicString(env, jstr) {
        if (env && jstr) {
            data_ = env->GetStringUTFChars(jstr, nullptr);
        } else if (default_str) {
            data_ = default_str->data();
        } else {
            data_ = nullptr;
        }
    }

    operator const std::string() const { return data_; }
    operator const std::string_view() const { return data_; }

    ~JUTFString() {
        if (env_ && jstr_) {
            env_->ReleaseStringUTFChars(jstr_, data_);
        }
    }

    JUTFString(JUTFString &&other)
        : JBasicString(std::move(other.env_), std::move(other.jstr_), std::move(other.data_)) {
        other.data_ = nullptr;
    }

    JUTFString &operator=(JUTFString &&other) {
        if (&other != this) {
            env_ = std::move(other.env_);
            jstr_ = std::move(other.jstr_);
            data_ = std::move(other.data_);
            other.data_ = nullptr;
        }
        return *this;
    }
};

class JString : public JBasicString<char16_t> {
public:
    JString(const ScopedLocalRef<jstring> &jstr) : JString(jstr.env_, jstr.local_ref_) {}

    JString(JNIEnv *env, jstring jstr, std::optional<std::u16string_view> default_str = {})
        : JBasicString(env, jstr) {
        if (env && jstr) {
            data_ = reinterpret_cast<const char16_t *>(env->GetStringChars(jstr, &is_copy_));
            size_ = env->GetStringLength(jstr);
        } else if (default_str) {
            data_ = default_str->data();
            size_ = default_str->size();
        } else {
            data_ = nullptr;
            size_ = 0;
        }
    }

    operator const std::u16string() const { return {data_, static_cast<size_t>(size_)}; }
    operator const std::u16string_view() const { return {data_, static_cast<size_t>(size_)}; }

    auto size() const { return size_; }

    auto is_copy() const { return static_cast<bool>(is_copy_); }

    ~JString() {
        if (env_ && jstr_) {
            env_->ReleaseStringChars(jstr_, reinterpret_cast<const jchar *>(data_));
        }
    }

    JString(JString &&other)
        : JBasicString(std::move(other.env_), std::move(other.jstr_), std::move(other.data_)),
          size_(std::move(other.size_)),
          is_copy_(std::move(other.is_copy_)) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    JString &operator=(JString &&other) {
        if (&other != this) {
            env_ = std::move(other.env_);
            jstr_ = std::move(other.jstr_);
            data_ = std::move(other.data_);
            size_ = std::move(other.size_);
            is_copy_ = std::move(other.is_copy_);
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

private:
    jsize size_;
    jboolean is_copy_{};
};

template <typename Result = std::nullptr_t, typename Func, typename... Args>
    requires(std::is_function_v<Func>)
[[maybe_unused]] inline auto JNI_Invoke(JNIEnv *env, Func JNIEnv::*f, Args &&...args) {
    using FuncReturnType =
        std::invoke_result_t<Func, decltype(UnwrapScope(std::forward<Args>(args)))...>;
    using ResultType = std::conditional_t<std::is_null_pointer_v<Result>, FuncReturnType, Result>;

    if constexpr (std::is_void_v<FuncReturnType>) {
        (env->*f)(UnwrapScope(std::forward<Args>(args))...);
    } else {
        auto result = (env->*f)(UnwrapScope(std::forward<Args>(args))...);
        if constexpr (std::is_integral_v<FuncReturnType> && std::is_integral_v<ResultType>) {
            return static_cast<ResultType>(result);
        } else {
            return WrapScope(env, reinterpret_cast<ResultType>(result));
        }
    }
}

class JNISafeInvocationFinally {
    JNIEnv *env_;

    LSPLANT_DISALLOW_COPY_AND_ASSIGN(JNISafeInvocationFinally);

public:
    JNISafeInvocationFinally(JNIEnv *env) : env_(env) {}

    ~JNISafeInvocationFinally() {
#ifdef JNI_HELPER_LOG_DISABLED
        env_->ExceptionClear();
#else
        if (!env_->ExceptionCheck()) [[likely]] {
            return;
        }
        auto exception = ClearException(env_);
        __android_log_write(ANDROID_LOG_ERROR,
#ifdef LOG_TAG
                            LOG_TAG,
#else
                            "JNIHelper",
#endif
                            JUTFString(env_, exception.get()).get());
#endif
    }

private:
    inline static ScopedLocalRef<jstring> ClearException(JNIEnv *env) {
        auto exception = env->ExceptionOccurred();
        env->ExceptionClear();
        auto log = env->FindClass("android/util/Log");
        static auto toString = env->GetStaticMethodID(log, "getStackTraceString",
                                                      "(Ljava/lang/Throwable;)Ljava/lang/String;");
        auto str = reinterpret_cast<jstring>(env->CallStaticObjectMethod(log, toString, exception));
        env->DeleteLocalRef(log);
        env->DeleteLocalRef(exception);
        return {env, str};
    }
};

template <typename Result = std::nullptr_t, typename Func, typename... Args>
    requires(std::is_function_v<Func>)
[[maybe_unused]] inline auto JNI_SafeInvoke(JNIEnv *env, Func JNIEnv::*f, Args &&...args) {
    JNISafeInvocationFinally finally{env};
    return JNI_Invoke<Result>(env, f, std::forward<Args>(args)...);
}

// functions to class

[[maybe_unused]] inline auto JNI_FindClass(JNIEnv *env, std::string_view name) {
    return JNI_SafeInvoke(env, &JNIEnv::FindClass, name);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetSuperclass(JNIEnv *env, const Class &clazz) {
    return JNI_Invoke(env, &JNIEnv::GetSuperclass, clazz);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetObjectClass(JNIEnv *env, const Object &obj) {
    return JNI_Invoke(env, &JNIEnv::GetObjectClass, obj);
}

// functions to field

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetFieldID(JNIEnv *env, Class &&clazz, std::string_view name,
                                            std::string_view sig) {
    return JNI_SafeInvoke(env, &JNIEnv::GetFieldID, std::forward<Class>(clazz), name, sig);
}

// getters

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetObjectField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetObjectField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetBooleanField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetBooleanField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetByteField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetByteField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetCharField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetCharField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetShortField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetShortField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrJCompatiblePointer Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetIntField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetIntField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrJCompatiblePointer Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetLongField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetLongField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetFloatField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke(env, &JNIEnv::GetFloatField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetDoubleField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    return JNI_Invoke(env, &JNIEnv::GetDoubleField, std::forward<Object>(obj), fieldId);
}

template <ScopeOrJCompatiblePointer Result = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetField(JNIEnv *env, Object &&obj, jfieldID fieldId) {
    if constexpr (std::is_same_v<Result, jboolean>) {
        return JNI_GetBooleanField(env, std::forward<Object>(obj), fieldId);
    } else if constexpr (std::is_same_v<Result, jbyte>) {
        return JNI_GetByteField(env, std::forward<Object>(obj), fieldId);
    } else if constexpr (std::is_same_v<Result, jshort>) {
        return JNI_GetShortField(env, std::forward<Object>(obj), fieldId);
    } else if constexpr (std::is_same_v<Result, jchar>) {
        return JNI_GetCharField(env, std::forward<Object>(obj), fieldId);
    } else if constexpr (std::is_same_v<Result, jint> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jint))) {
        return JNI_GetIntField<Result>(env, std::forward<Object>(obj), fieldId);
    } else if constexpr (std::is_same_v<Result, jfloat>) {
        return JNI_GetFloatField(env, std::forward<Object>(obj), fieldId);
    } else if constexpr (std::is_same_v<Result, jlong> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jlong))) {
        return JNI_GetLongField<Result>(env, std::forward<Object>(obj), fieldId);
    } else if constexpr (std::is_same_v<Result, jdouble>) {
        return JNI_GetDoubleField(env, std::forward<Object>(obj), fieldId);
    } else {
        std::unreachable();
    }
}

// setters

template <ScopeOrObject Object, ScopeOrObject Value>
[[maybe_unused]] inline auto JNI_SetObjectField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                                const Value &value) {
    return JNI_Invoke(env, &JNIEnv::SetObjectField, std::forward<Object>(obj), fieldId, value);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetBooleanField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                                 jboolean value) {
    return JNI_Invoke(env, &JNIEnv::SetBooleanField, std::forward<Object>(obj), fieldId, value);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetByteField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                              jbyte value) {
    return JNI_Invoke(env, &JNIEnv::SetByteField, std::forward<Object>(obj), fieldId, value);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetCharField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                              jchar value) {
    return JNI_Invoke(env, &JNIEnv::SetCharField, std::forward<Object>(obj), fieldId, value);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetShortField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                               jshort value) {
    return JNI_Invoke(env, &JNIEnv::SetShortField, std::forward<Object>(obj), fieldId, value);
}

template <ScopeOrJCompatiblePointer Value = jint, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetIntField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                             Value value) {
    return JNI_Invoke(env, &JNIEnv::SetIntField, std::forward<Object>(obj), fieldId,
                      reinterpret_cast<jint>(value));
}

template <ScopeOrJCompatiblePointer Value = jlong, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetLongField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                              Value value) {
    return JNI_Invoke(env, &JNIEnv::SetLongField, std::forward<Object>(obj), fieldId,
                      reinterpret_cast<jlong>(value));
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetFloatField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                               jfloat value) {
    return JNI_Invoke(env, &JNIEnv::SetFloatField, std::forward<Object>(obj), fieldId, value);
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetDoubleField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                                jdouble value) {
    return JNI_Invoke(env, &JNIEnv::SetDoubleField, std::forward<Object>(obj), fieldId, value);
}

template <ScopeOrJCompatiblePointer Value = std::nullptr_t, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetField(JNIEnv *env, Object &&obj, jfieldID fieldId,
                                          Value value) {
    if constexpr (std::is_same_v<Value, jboolean>) {
        return JNI_SetBooleanField(env, std::forward<Object>(obj), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jbyte>) {
        return JNI_SetByteField(env, std::forward<Object>(obj), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jshort>) {
        return JNI_SetShortField(env, std::forward<Object>(obj), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jchar>) {
        return JNI_SetCharField(env, std::forward<Object>(obj), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jint> ||
                         (std::is_pointer_v<Value> && sizeof(Value) == sizeof(jint))) {
        return JNI_SetIntField<Value>(env, std::forward<Object>(obj), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jfloat>) {
        return JNI_SetFloatField(env, std::forward<Object>(obj), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jlong> ||
                         (std::is_pointer_v<Value> && sizeof(Value) == sizeof(jlong))) {
        return JNI_SetLongField<Value>(env, std::forward<Object>(obj), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jdouble>) {
        return JNI_SetDoubleField(env, std::forward<Object>(obj), fieldId, value);
    } else {
        std::unreachable();
    }
}

// functions to static field

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticFieldID(JNIEnv *env, Class &&clazz, std::string_view name,
                                                  std::string_view sig) {
    return JNI_SafeInvoke(env, &JNIEnv::GetStaticFieldID, std::forward<Class>(clazz), name, sig);
}

// getters

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticObjectField(JNIEnv *env, Class &&clazz,
                                                      jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetStaticObjectField, std::forward<Class>(clazz),
                              fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticBooleanField(JNIEnv *env, Class &&clazz,
                                                       jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetStaticBooleanField, std::forward<Class>(clazz),
                              fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticByteField(JNIEnv *env, Class &&clazz, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetStaticByteField, std::forward<Class>(clazz),
                              fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticCharField(JNIEnv *env, Class &&clazz, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetStaticCharField, std::forward<Class>(clazz),
                              fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticShortField(JNIEnv *env, Class &&clazz, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetStaticShortField, std::forward<Class>(clazz),
                              fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticIntField(JNIEnv *env, Class &&clazz, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetStaticIntField, std::forward<Class>(clazz), fieldId);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticLongField(JNIEnv *env, Class &&clazz, jfieldID fieldId) {
    return JNI_Invoke<Result>(env, &JNIEnv::GetStaticLongField, std::forward<Class>(clazz),
                              fieldId);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticFloatField(JNIEnv *env, Class &&clazz, jfieldID fieldId) {
    return JNI_Invoke(env, &JNIEnv::GetStaticFloatField, std::forward<Class>(clazz), fieldId);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticDoubleField(JNIEnv *env, Class &&clazz,
                                                      jfieldID fieldId) {
    return JNI_Invoke(env, &JNIEnv::GetStaticDoubleField, std::forward<Class>(clazz), fieldId);
}

template <ScopeOrJCompatiblePointer Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticField(JNIEnv *env, Class &&clazz, jfieldID fieldId) {
    if constexpr (std::is_same_v<Result, jboolean>) {
        return JNI_GetStaticBooleanField(env, std::forward<Class>(clazz), fieldId);
    } else if constexpr (std::is_same_v<Result, jbyte>) {
        return JNI_GetStaticByteField(env, std::forward<Class>(clazz), fieldId);
    } else if constexpr (std::is_same_v<Result, jshort>) {
        return JNI_GetStaticShortField(env, std::forward<Class>(clazz), fieldId);
    } else if constexpr (std::is_same_v<Result, jchar>) {
        return JNI_GetStaticCharField(env, std::forward<Class>(clazz), fieldId);
    } else if constexpr (std::is_same_v<Result, jint> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jint))) {
        return JNI_GetStaticIntField<Result>(env, std::forward<Class>(clazz), fieldId);
    } else if constexpr (std::is_same_v<Result, jfloat>) {
        return JNI_GetStaticFloatField(env, std::forward<Class>(clazz), fieldId);
    } else if constexpr (std::is_same_v<Result, jlong> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jlong))) {
        return JNI_GetStaticLongField<Result>(env, std::forward<Class>(clazz), fieldId);
    } else if constexpr (std::is_same_v<Result, jdouble>) {
        return JNI_GetStaticDoubleField(env, std::forward<Class>(clazz), fieldId);
    } else {
        std::unreachable();
    }
}
// setters

template <ScopeOrClass Class, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_SetStaticObjectField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                      const Object &value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticObjectField, std::forward<Class>(clazz), fieldId,
                      value);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticBooleanField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                       jboolean value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticBooleanField, std::forward<Class>(clazz), fieldId,
                      value);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticByteField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                    jbyte value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticByteField, std::forward<Class>(clazz), fieldId, value);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticCharField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                    jchar value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticCharField, std::forward<Class>(clazz), fieldId, value);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticShortField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                     jshort value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticShortField, std::forward<Class>(clazz), fieldId,
                      value);
}

template <ScopeOrJCompatiblePointer Value = jint, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticIntField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                   Value value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticIntField, std::forward<Class>(clazz), fieldId,
                      reinterpret_cast<jint>(value));
}

template <ScopeOrJCompatiblePointer Value = jlong, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticLongField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                    Value value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticLongField, std::forward<Class>(clazz), fieldId,
                      reinterpret_cast<jlong>(value));
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticFloatField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                     jfloat value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticFloatField, std::forward<Class>(clazz), fieldId,
                      value);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticDoubleField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                      jdouble value) {
    return JNI_Invoke(env, &JNIEnv::SetStaticDoubleField, std::forward<Class>(clazz), fieldId,
                      value);
}

template <ScopeOrJCompatiblePointer Value = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_SetStaticField(JNIEnv *env, Class &&clazz, jfieldID fieldId,
                                                Value value) {
    if constexpr (std::is_same_v<Value, jboolean>) {
        return JNI_SetStaticBooleanField(env, std::forward<Class>(clazz), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jbyte>) {
        return JNI_SetStaticByteField(env, std::forward<Class>(clazz), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jshort>) {
        return JNI_SetStaticShortField(env, std::forward<Class>(clazz), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jchar>) {
        return JNI_SetStaticCharField(env, std::forward<Class>(clazz), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jint> ||
                         (std::is_pointer_v<Value> && sizeof(Value) == sizeof(jint))) {
        return JNI_SetStaticIntField<Value>(env, std::forward<Class>(clazz), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jfloat>) {
        return JNI_SetStaticFloatField(env, std::forward<Class>(clazz), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jlong> ||
                         (std::is_pointer_v<Value> && sizeof(Value) == sizeof(jlong))) {
        return JNI_SetStaticLongField<Value>(env, std::forward<Class>(clazz), fieldId, value);
    } else if constexpr (std::is_same_v<Value, jdouble>) {
        return JNI_SetStaticDoubleField(env, std::forward<Class>(clazz), fieldId, value);
    } else {
        std::unreachable();
    }
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_ToReflectedMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                   jboolean isStatic = JNI_FALSE) {
    return JNI_Invoke(env, &JNIEnv::ToReflectedMethod, std::forward<Class>(clazz), method,
                      isStatic);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_ToReflectedField(JNIEnv *env, Class &&clazz, jfieldID field,
                                                  jboolean isStatic = JNI_FALSE) {
    return JNI_Invoke(env, &JNIEnv::ToReflectedField, std::forward<Class>(clazz), field, isStatic);
}

// functions to method

// virtual methods

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetMethodID(JNIEnv *env, Class &&clazz, std::string_view name,
                                             std::string_view sig) {
    return JNI_SafeInvoke(env, &JNIEnv::GetMethodID, std::forward<Class>(clazz), name, sig);
}

template <ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallVoidMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallVoidMethod, std::forward<Object>(obj), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallObjectMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                  Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallObjectMethod, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallBooleanMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                   Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallBooleanMethod, std::forward<Object>(obj),
                                  method, std::forward<Args>(args)...);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallByteMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallByteMethod, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallCharMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallCharMethod, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallShortMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                 Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallShortMethod, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = jint, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallIntMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                               Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallIntMethod, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = jlong, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallLongMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallLongMethod, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallFloatMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                 Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallFloatMethod, std::forward<Object>(obj), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallDoubleMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                                  Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallDoubleMethod, std::forward<Object>(obj), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = std::nullptr_t, ScopeOrObject Object, typename... Args>
[[maybe_unused]] inline auto JNI_CallMethod(JNIEnv *env, Object &&obj, jmethodID method,
                                            Args &&...args) {
    if constexpr (std::is_same_v<Result, jboolean>) {
        return JNI_CallBooleanMethod(env, std::forward<Object>(obj), method,
                                     std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jbyte>) {
        return JNI_CallByteMethod(env, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jshort>) {
        return JNI_CallShortMethod(env, std::forward<Object>(obj), method,
                                   std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jchar>) {
        return JNI_CallCharMethod(env, std::forward<Object>(obj), method,
                                  std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jint> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jint))) {
        return JNI_CallIntMethod<Result>(env, std::forward<Object>(obj), method,
                                         std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jfloat>) {
        return JNI_CallFloatMethod(env, std::forward<Object>(obj), method,
                                   std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jlong> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jlong))) {
        return JNI_CallLongMethod<Result>(env, std::forward<Object>(obj), method,
                                          std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jdouble>) {
        return JNI_CallDoubleMethod(env, std::forward<Object>(obj), method,
                                    std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, void>) {
        JNI_CallVoidMethod(env, std::forward<Object>(obj), method, std::forward<Args>(args)...);
    } else {
        std::unreachable();
    }
}

// static methods

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_GetStaticMethodID(JNIEnv *env, Class &&clazz,
                                                   std::string_view name, std::string_view sig) {
    return JNI_SafeInvoke(env, &JNIEnv::GetStaticMethodID, std::forward<Class>(clazz), name, sig);
}

template <ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticVoidMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                      Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallStaticVoidMethod, std::forward<Class>(clazz), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticObjectMethod(JNIEnv *env, Class &&clazz,
                                                        jmethodID method, Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallStaticObjectMethod, std::forward<Class>(clazz),
                                  method, std::forward<Args>(args)...);
}

template <ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticBooleanMethod(JNIEnv *env, Class &&clazz,
                                                         jmethodID method, Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallStaticBooleanMethod, std::forward<Class>(clazz), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticByteMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                      Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallStaticByteMethod, std::forward<Class>(clazz), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticCharMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                      Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallStaticCharMethod, std::forward<Class>(clazz), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticShortMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                       Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallStaticShortMethod, std::forward<Class>(clazz), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = jint, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticIntMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                     Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallStaticIntMethod, std::forward<Class>(clazz),
                                  method, std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = jlong, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticLongMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                      Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallStaticLongMethod, std::forward<Class>(clazz),
                                  method, std::forward<Args>(args)...);
}

template <ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticFloatMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                       Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallStaticFloatMethod, std::forward<Class>(clazz), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticDoubleMethod(JNIEnv *env, Class &&clazz,
                                                        jmethodID method, Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallStaticDoubleMethod, std::forward<Class>(clazz), method,
                          std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = std::nullptr_t, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallStaticMethod(JNIEnv *env, Class &&clazz, jmethodID method,
                                                  Args &&...args) {
    if constexpr (std::is_same_v<Result, jboolean>) {
        return JNI_CallStaticBooleanMethod(env, std::forward<Class>(clazz), method,
                                           std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jbyte>) {
        return JNI_CallStaticByteMethod(env, std::forward<Class>(clazz), method,
                                        std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jshort>) {
        return JNI_CallStaticShortMethod(env, std::forward<Class>(clazz), method,
                                         std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jchar>) {
        return JNI_CallStaticCharMethod(env, std::forward<Class>(clazz), method,
                                        std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jint> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jint))) {
        return JNI_CallStaticIntMethod<Result>(env, std::forward<Class>(clazz), method,
                                               std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jfloat>) {
        return JNI_CallStaticFloatMethod(env, std::forward<Class>(clazz), method,
                                         std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jlong> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jlong))) {
        return JNI_CallStaticLongMethod<Result>(env, std::forward<Class>(clazz), method,
                                                std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jdouble>) {
        return JNI_CallStaticDoubleMethod(env, std::forward<Class>(clazz), method,
                                          std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, void>) {
        JNI_CallStaticVoidMethod(env, std::forward<Class>(clazz), method,
                                 std::forward<Args>(args)...);
    } else {
        std::unreachable();
    }
}

// non-virtual methods

template <ScopeOrObject Object, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualVoidMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                          jmethodID method, Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallNonvirtualVoidMethod, std::forward<Object>(obj),
                          std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrObject Object, ScopeOrClass Class,
          typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualObjectMethod(JNIEnv *env, Object &&obj,
                                                            Class &&clazz, jmethodID method,
                                                            Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallNonvirtualObjectMethod,
                                  std::forward<Object>(obj), std::forward<Class>(clazz), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrObject Object, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualBooleanMethod(JNIEnv *env, Object &&obj,
                                                             Class &&clazz, jmethodID method,
                                                             Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallNonvirtualBooleanMethod, std::forward<Object>(obj),
                          std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrObject Object, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualByteMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                          jmethodID method, Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallNonvirtualByteMethod, std::forward<Object>(obj),
                          std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrObject Object, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualCharMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                          jmethodID method, Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallNonvirtualCharMethod, std::forward<Object>(obj),
                          std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrObject Object, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualShortMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                           jmethodID method, Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallNonvirtualShortMethod, std::forward<Object>(obj),
                          std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = jint, ScopeOrObject Object, ScopeOrClass Class,
          typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualIntMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                         jmethodID method, Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallNonvirtualIntMethod, std::forward<Object>(obj),
                                  std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = jlong, ScopeOrObject Object, ScopeOrClass Class,
          typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualLongMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                          jmethodID method, Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::CallNonvirtualLongMethod, std::forward<Object>(obj),
                                  std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrObject Object, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualFloatMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                           jmethodID method, Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallNonvirtualFloatMethod, std::forward<Object>(obj),
                          std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrObject Object, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualDoubleMethod(JNIEnv *env, Object &&obj,
                                                            Class &&clazz, jmethodID method,
                                                            Args &&...args) {
    return JNI_SafeInvoke(env, &JNIEnv::CallNonvirtualDoubleMethod, std::forward<Object>(obj),
                          std::forward<Class>(clazz), method, std::forward<Args>(args)...);
}

template <ScopeOrJCompatiblePointer Result = std::nullptr_t, ScopeOrObject Object,
          ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_CallNonvirtualMethod(JNIEnv *env, Object &&obj, Class &&clazz,
                                                      jmethodID method, Args &&...args) {
    if constexpr (std::is_same_v<Result, jboolean>) {
        return JNI_CallNonvirtualBooleanMethod(env, std::forward<Object>(obj),
                                               std::forward<Class>(clazz), method,
                                               std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jbyte>) {
        return JNI_CallNonvirtualByteMethod(env, std::forward<Object>(obj),
                                            std::forward<Class>(clazz), method,
                                            std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jshort>) {
        return JNI_CallNonvirtualShortMethod(env, std::forward<Object>(obj),
                                             std::forward<Class>(clazz), method,
                                             std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jchar>) {
        return JNI_CallNonvirtualCharMethod(env, std::forward<Object>(obj),
                                            std::forward<Class>(clazz), method,
                                            std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jint> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jint))) {
        return JNI_CallNonvirtualIntMethod<Result>(env, std::forward<Object>(obj),
                                                   std::forward<Class>(clazz), method,
                                                   std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jfloat>) {
        return JNI_CallNonvirtualFloatMethod(env, std::forward<Object>(obj),
                                             std::forward<Class>(clazz), method,
                                             std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jlong> ||
                         (std::is_pointer_v<Result> && sizeof(Result) == sizeof(jlong))) {
        return JNI_CallNonvirtualLongMethod<Result>(env, std::forward<Object>(obj),
                                                    std::forward<Class>(clazz), method,
                                                    std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, jdouble>) {
        return JNI_CallNonvirtualDoubleMethod(env, std::forward<Object>(obj),
                                              std::forward<Class>(clazz), method,
                                              std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<Result, void>) {
        JNI_CallNonvirtualVoidMethod(env, std::forward<Object>(obj), std::forward<Class>(clazz),
                                     method, std::forward<Args>(args)...);
    } else {
        std::unreachable();
    }
}

template <ScopeOrObject Result = std::nullptr_t, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_AllocObject(JNIEnv *env, Class &&clazz) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::AllocObject, std::forward<Class>(clazz));
}

template <ScopeOrJCompatible Result = std::nullptr_t, ScopeOrClass Class, typename... Args>
[[maybe_unused]] inline auto JNI_NewObject(JNIEnv *env, Class &&clazz, jmethodID method,
                                           Args &&...args) {
    return JNI_SafeInvoke<Result>(env, &JNIEnv::NewObject, std::forward<Class>(clazz), method,
                                  std::forward<Args>(args)...);
}

template <ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_RegisterNatives(JNIEnv *env, Class &&clazz,
                                                 const JNINativeMethod *methods, jint size) {
    return JNI_SafeInvoke(env, &JNIEnv::RegisterNatives, std::forward<Class>(clazz), methods,
                          size) == JNI_OK;
}

template <ScopeOrClass Class, size_t N>
[[maybe_unused]] inline auto JNI_RegisterNatives(JNIEnv *env, Class &&clazz,
                                                 const JNINativeMethod (&methods)[N]) {
    static_assert(N > 0, "empty methods");
    return JNI_RegisterNatives(env, std::forward<Class>(clazz), methods, N);
}

template <ScopeOrClass Class, size_t N>
[[maybe_unused]] inline auto JNI_RegisterNatives(JNIEnv *env, Class &&clazz,
                                                 std::array<JNINativeMethod, N> methods) {
    static_assert(N > 0, "empty methods");
    return JNI_RegisterNatives(env, std::forward<Class>(clazz), methods.data(), N);
}

template <ScopeOrObject Object, ScopeOrClass Class>
[[maybe_unused]] inline auto JNI_IsInstanceOf(JNIEnv *env, Object &&obj, Class &&clazz) {
    return env->IsInstanceOf(UnwrapScope(std::forward<Object>(obj)),
                             UnwrapScope(std::forward<Class>(clazz)));
}

template <ScopeOrClass Class1, ScopeOrClass Class2>
[[maybe_unused]] inline auto JNI_IsAssignableFrom(JNIEnv *env, Class1 &&a, Class2 &&b) {
    return env->IsAssignableFrom(UnwrapScope(std::forward<Class1>(a)),
                                 UnwrapScope(std::forward<Class2>(b)));
}

template <ScopeOrObject Object1, ScopeOrObject Object2>
[[maybe_unused]] inline auto JNI_IsSameObject(JNIEnv *env, Object1 &&a, Object2 &&b) {
    return env->IsSameObject(UnwrapScope(std::forward<Object1>(a)),
                             UnwrapScope(std::forward<Object2>(b)));
}

template <ScopeOrObject Object, ScopeOrObject Result = Object>
[[maybe_unused]] inline auto JNI_NewLocalRef(JNIEnv *env, Object &&x) {
    return JNI_Invoke<Result>(env, &JNIEnv::NewLocalRef, std::forward<Object>(x));
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_NewGlobalRef(JNIEnv *env, Object &&x) {
    return (decltype(UnwrapScope(std::forward<Object>(x))))env->NewGlobalRef(
        UnwrapScope(std::forward<Object>(x)));
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_NewWeakGlobalRef(JNIEnv *env, Object &&x) {
    return (decltype(UnwrapScope(std::forward<Object>(x))))env->NewWeakGlobalRef(
        UnwrapScope(std::forward<Object>(x)));
}

template <typename U, typename T>
[[maybe_unused]] inline auto JNI_Cast(ScopedLocalRef<T> &&x)
    requires(std::is_convertible_v<T, _jobject *>)
{
    return ScopedLocalRef<U>(std::move(x));
}

template <typename U>
[[maybe_unused]] inline auto JNI_Cast(JObjectArrayElement &&x) {
    return JNI_Cast<U, jobject>(std::move(x));
}

[[maybe_unused]] inline auto JNI_NewDirectByteBuffer(JNIEnv *env, void *address, jlong capacity) {
    return JNI_Invoke(env, &JNIEnv::NewDirectByteBuffer, address, capacity);
}

template <JArray T>
struct JArrayUnderlyingTypeHelper;

template <>
struct JArrayUnderlyingTypeHelper<jbooleanArray> {
    using Type = jboolean;
};

template <>
struct JArrayUnderlyingTypeHelper<jbyteArray> {
    using Type = jbyte;
};

template <>
struct JArrayUnderlyingTypeHelper<jcharArray> {
    using Type = jchar;
};

template <>
struct JArrayUnderlyingTypeHelper<jshortArray> {
    using Type = jshort;
};

template <>
struct JArrayUnderlyingTypeHelper<jintArray> {
    using Type = jint;
};

template <>
struct JArrayUnderlyingTypeHelper<jlongArray> {
    using Type = jlong;
};

template <>
struct JArrayUnderlyingTypeHelper<jfloatArray> {
    using Type = jfloat;
};

template <>
struct JArrayUnderlyingTypeHelper<jdoubleArray> {
    using Type = jdouble;
};

template <JArray T>
using JArrayUnderlyingType = typename JArrayUnderlyingTypeHelper<T>::Type;

template <JArray T>
class ScopedLocalRef<T> {
public:
    class Iterator {
        friend class ScopedLocalRef<T>;
        Iterator(JArrayUnderlyingType<T> *e) : e_(e) {}
        JArrayUnderlyingType<T> *e_;

    public:
        auto &operator*() { return *e_; }
        auto *operator->() { return e_; }
        Iterator &operator++() { return ++e_, *this; }
        Iterator &operator--() { return --e_, *this; }
        Iterator operator++(int) { return Iterator(e_++); }
        Iterator operator--(int) { return Iterator(e_--); }
        bool operator==(const Iterator &other) const { return other.e_ == e_; }
        bool operator!=(const Iterator &other) const { return other.e_ != e_; }
    };

    class ConstIterator {
        friend class ScopedLocalRef<T>;
        ConstIterator(const JArrayUnderlyingType<T> *e) : e_(e) {}
        const JArrayUnderlyingType<T> *e_;

    public:
        const auto &operator*() { return *e_; }
        const auto *operator->() { return e_; }
        ConstIterator &operator++() { return ++e_, *this; }
        ConstIterator &operator--() { return --e_, *this; }
        ConstIterator operator++(int) { return ConstIterator(e_++); }
        ConstIterator operator--(int) { return ConstIterator(e_--); }
        bool operator==(const ConstIterator &other) const { return other.e_ == e_; }
        bool operator!=(const ConstIterator &other) const { return other.e_ != e_; }
    };

    auto begin() {
        modified_ = true;
        return Iterator(elements_);
    }

    auto end() {
        modified_ = true;
        return Iterator(elements_ + size_);
    }

    const auto begin() const { return ConstIterator(elements_); }

    auto end() const { return ConstIterator(elements_ + size_); }

    const auto cbegin() const { return ConstIterator(elements_); }

    auto cend() const { return ConstIterator(elements_ + size_); }

    using BaseType [[maybe_unused]] = T;

    ScopedLocalRef(JNIEnv *env, T local_ref) noexcept : env_(env), local_ref_(nullptr) {
        reset(local_ref);
    }

    ScopedLocalRef(ScopedLocalRef &&s) noexcept { *this = std::move(s); }

    template <JObject U>
    ScopedLocalRef(ScopedLocalRef<U> &&s) noexcept : ScopedLocalRef(s.env_, (T)s.release()) {}

    explicit ScopedLocalRef(JNIEnv *env) noexcept : ScopedLocalRef(env, T{nullptr}) {}

    ~ScopedLocalRef() { env_->DeleteLocalRef(release()); }

    void reset(T ptr = nullptr) {
        if (ptr != local_ref_) {
            if (local_ref_ != nullptr) {
                ReleaseElements(modified_ ? 0 : JNI_ABORT);
                env_->DeleteLocalRef(local_ref_);
                elements_ = nullptr;
            }
            local_ref_ = ptr;
            size_ = local_ref_ ? env_->GetArrayLength(local_ref_) : 0;
            if (!local_ref_) return;
            static_assert(!std::is_same_v<T, jobjectArray>);
            if constexpr (std::is_same_v<T, jbooleanArray>) {
                elements_ = env_->GetBooleanArrayElements(local_ref_, nullptr);
            } else if constexpr (std::is_same_v<T, jbyteArray>) {
                elements_ = env_->GetByteArrayElements(local_ref_, nullptr);
            } else if constexpr (std::is_same_v<T, jcharArray>) {
                elements_ = env_->GetCharArrayElements(local_ref_, nullptr);
            } else if constexpr (std::is_same_v<T, jshortArray>) {
                elements_ = env_->GetShortArrayElements(local_ref_, nullptr);
            } else if constexpr (std::is_same_v<T, jintArray>) {
                elements_ = env_->GetIntArrayElements(local_ref_, nullptr);
            } else if constexpr (std::is_same_v<T, jlongArray>) {
                elements_ = env_->GetLongArrayElements(local_ref_, nullptr);
            } else if constexpr (std::is_same_v<T, jfloatArray>) {
                elements_ = env_->GetFloatArrayElements(local_ref_, nullptr);
            } else if constexpr (std::is_same_v<T, jdoubleArray>) {
                elements_ = env_->GetDoubleArrayElements(local_ref_, nullptr);
            }
        }
    }

    [[nodiscard]] T release() {
        T localRef = local_ref_;
        size_ = 0;
        local_ref_ = nullptr;
        ReleaseElements(modified_ ? 0 : JNI_ABORT);
        elements_ = nullptr;
        return localRef;
    }

    T get() const { return local_ref_; }

    JArrayUnderlyingType<T> &operator[](size_t index) {
        modified_ = true;
        return elements_[index];
    }

    const JArrayUnderlyingType<T> &operator[](size_t index) const { return elements_[index]; }

    void commit() {
        ReleaseElements(JNI_COMMIT);
        modified_ = false;
    }

    // We do not expose an empty constructor as it can easily lead to errors
    // using common idioms, e.g.:
    //   ScopedLocalRef<...> ref;
    //   ref.reset(...);
    // Move assignment operator.
    ScopedLocalRef &operator=(ScopedLocalRef &&s) noexcept {
        env_ = s.env_;
        local_ref_ = s.local_ref_;
        size_ = s.size_;
        elements_ = s.elements_;
        modified_ = s.modified_;
        s.elements_ = nullptr;
        s.size_ = 0;
        s.modified_ = false;
        s.local_ref_ = nullptr;
        return *this;
    }

    size_t size() const { return size_; }

    operator bool() const { return local_ref_; }

    template <JObject U>
    friend class ScopedLocalRef;

    friend class JUTFString;
    friend class JString;

    template <typename U>
    friend class JDirectBuffer;

private:
    void ReleaseElements(jint mode) {
        if (!local_ref_ || !elements_) return;
        if constexpr (std::is_same_v<T, jbooleanArray>) {
            env_->ReleaseBooleanArrayElements(local_ref_, elements_, mode);
        } else if constexpr (std::is_same_v<T, jbyteArray>) {
            env_->ReleaseByteArrayElements(local_ref_, elements_, mode);
        } else if constexpr (std::is_same_v<T, jcharArray>) {
            env_->ReleaseCharArrayElements(local_ref_, elements_, mode);
        } else if constexpr (std::is_same_v<T, jshortArray>) {
            env_->ReleaseShortArrayElements(local_ref_, elements_, mode);
        } else if constexpr (std::is_same_v<T, jintArray>) {
            env_->ReleaseIntArrayElements(local_ref_, elements_, mode);
        } else if constexpr (std::is_same_v<T, jlongArray>) {
            env_->ReleaseLongArrayElements(local_ref_, elements_, mode);
        } else if constexpr (std::is_same_v<T, jfloatArray>) {
            env_->ReleaseFloatArrayElements(local_ref_, elements_, mode);
        } else if constexpr (std::is_same_v<T, jdoubleArray>) {
            env_->ReleaseDoubleArrayElements(local_ref_, elements_, mode);
        }
    }

    JNIEnv *env_;
    T local_ref_;
    size_t size_;
    JArrayUnderlyingType<T> *elements_{nullptr};
    bool modified_ = false;
    LSPLANT_DISALLOW_COPY_AND_ASSIGN(ScopedLocalRef);
};

class JObjectArrayElement {
    friend class ScopedLocalRef<jobjectArray>;

    auto obtain() {
        if (i_ < 0 || i_ >= size_) return ScopedLocalRef<jobject>{nullptr};
        return JNI_Invoke(env_, &JNIEnv::GetObjectArrayElement, array_, i_);
    }

    explicit JObjectArrayElement(JNIEnv *env, jobjectArray array, int i, size_t size)
        : env_(env), array_(array), i_(i), size_(size), item_(obtain()) {}

    JObjectArrayElement &operator++() {
        ++i_;
        item_ = obtain();
        return *this;
    }

    JObjectArrayElement &operator--() {
        --i_;
        item_ = obtain();
        return *this;
    }

    JObjectArrayElement operator++(int) { return JObjectArrayElement(env_, array_, i_ + 1, size_); }

    JObjectArrayElement operator--(int) { return JObjectArrayElement(env_, array_, i_ - 1, size_); }

public:
    JObjectArrayElement(JObjectArrayElement &&s)
        : env_(s.env_), array_(s.array_), i_(s.i_), size_(s.size_), item_(std::move(s.item_)) {}

    operator ScopedLocalRef<jobject> &() & { return item_; }

    operator ScopedLocalRef<jobject> &&() && { return std::move(item_); }

    JObjectArrayElement &operator=(JObjectArrayElement &&s) {
        reset(s.item_.release());
        return *this;
    }

    JObjectArrayElement &operator=(const JObjectArrayElement &s) {
        reset(env_->NewLocalRef(s.item_.get()));
        return *this;
    }

    template <JObject T>
    JObjectArrayElement &operator=(ScopedLocalRef<T> &&s) {
        reset(s.release());
        return *this;
    }

    template <JObject T>
    JObjectArrayElement &operator=(const ScopedLocalRef<T> &s) {
        reset(s.clone());
        return *this;
    }

    JObjectArrayElement &operator=(jobject s) {
        reset(env_->NewLocalRef(s));
        return *this;
    }

    void reset(jobject item) {
        item_.reset(item);
        JNI_Invoke(env_, &JNIEnv::SetObjectArrayElement, array_, i_, item_);
    }

    ScopedLocalRef<jobject> clone() const { return item_.clone(); }

    jobject get() const { return item_.get(); }

    jobject release() { return item_.release(); }

    jobject operator->() const { return item_.get(); }

    jobject operator*() const { return item_.get(); }

private:
    JNIEnv *env_;
    jobjectArray array_;
    int i_;
    int size_;
    ScopedLocalRef<jobject> item_;
    JObjectArrayElement(const JObjectArrayElement &) = delete;
};

template <>
class ScopedLocalRef<jobjectArray> {
public:
    class Iterator {
        friend class ScopedLocalRef<jobjectArray>;

        Iterator(JObjectArrayElement &&e) : e_(std::move(e)) {}
        Iterator(JNIEnv *env, jobjectArray array, int i, size_t size) : e_(env, array, i, size) {}

    public:
        auto &operator*() { return e_; }

        auto *operator->() { return e_.get(); }

        Iterator &operator++() {
            ++e_;
            return *this;
        }

        Iterator &operator--() {
            --e_;
            return *this;
        }

        Iterator operator++(int) { return Iterator(e_++); }

        Iterator operator--(int) { return Iterator(e_--); }

        bool operator==(const Iterator &other) const { return other.e_.i_ == e_.i_; }

        bool operator!=(const Iterator &other) const { return other.e_.i_ != e_.i_; }

    private:
        JObjectArrayElement e_;
    };

    class ConstIterator {
        friend class ScopedLocalRef<jobjectArray>;

        auto obtain() {
            if (i_ < 0 || i_ >= size_) return ScopedLocalRef<jobject>{nullptr};
            return JNI_Invoke(env_, &JNIEnv::GetObjectArrayElement, array_, i_);
        }

        ConstIterator(JNIEnv *env, jobjectArray array, int i, int size)
            : env_(env), array_(array), i_(i), size_(size), item_(obtain()) {}

    public:
        auto &operator*() { return item_; }

        auto *operator->() { return &item_; }

        ConstIterator &operator++() {
            ++i_;
            item_ = obtain();
            return *this;
        }

        ConstIterator &operator--() {
            --i_;
            item_ = obtain();
            return *this;
        }

        ConstIterator operator++(int) { return ConstIterator(env_, array_, i_ + 1, size_); }

        ConstIterator operator--(int) { return ConstIterator(env_, array_, i_ - 1, size_); }

        bool operator==(const ConstIterator &other) const { return other.i_ == i_; }

        bool operator!=(const ConstIterator &other) const { return other.i_ != i_; }

    private:
        JNIEnv *env_;
        jobjectArray array_;
        int i_;
        int size_;
        ScopedLocalRef<jobject> item_;
    };

    auto begin() { return Iterator(env_, local_ref_, 0, size_); }

    auto end() { return Iterator(env_, local_ref_, size_, size_); }

    const auto begin() const { return ConstIterator(env_, local_ref_, 0, size_); }

    auto end() const { return ConstIterator(env_, local_ref_, size_, size_); }

    const auto cbegin() const { return ConstIterator(env_, local_ref_, 0, size_); }

    auto cend() const { return ConstIterator(env_, local_ref_, size_, size_); }

    ScopedLocalRef(JNIEnv *env, jobjectArray local_ref) noexcept : env_(env), local_ref_(nullptr) {
        reset(local_ref);
    }

    ScopedLocalRef(ScopedLocalRef &&s) noexcept { *this = std::move(s); }

    template <JObject U>
    ScopedLocalRef(ScopedLocalRef<U> &&s) noexcept
        : ScopedLocalRef(s.env_, (jobjectArray)s.release()) {}

    explicit ScopedLocalRef(JNIEnv *env) noexcept : ScopedLocalRef(env, jobjectArray{nullptr}) {}

    ~ScopedLocalRef() { env_->DeleteLocalRef(release()); }

    void reset(jobjectArray ptr = nullptr) {
        if (ptr != local_ref_) {
            if (local_ref_ != nullptr) {
                env_->DeleteLocalRef(local_ref_);
            }
            local_ref_ = ptr;
            size_ = local_ref_ ? env_->GetArrayLength(local_ref_) : 0;
            if (!local_ref_) return;
        }
    }

    [[nodiscard]] jobjectArray release() {
        jobjectArray localRef = local_ref_;
        size_ = 0;
        local_ref_ = nullptr;
        return localRef;
    }

    jobjectArray get() const { return local_ref_; }

    JObjectArrayElement operator[](size_t index) {
        return JObjectArrayElement(env_, local_ref_, index, size_);
    }

    const ScopedLocalRef<jobject> operator[](size_t index) const {
        return JNI_Invoke(env_, &JNIEnv::GetObjectArrayElement, local_ref_, index);
    }

    // We do not expose an empty constructor as it can easily lead to errors
    // using common idioms, e.g.:
    //   ScopedLocalRef<...> ref;
    //   ref.reset(...);
    // Move assignment operator.
    ScopedLocalRef &operator=(ScopedLocalRef &&s) noexcept {
        env_ = s.env_;
        local_ref_ = s.local_ref_;
        size_ = s.size_;
        s.size_ = 0;
        s.local_ref_ = nullptr;
        return *this;
    }

    size_t size() const { return size_; }

    operator bool() const { return local_ref_; }

    template <JObject U>
    friend class ScopedLocalRef;

    friend class JUTFString;
    friend class JString;

    template <typename U>
    friend class JDirectBuffer;

private:
    JNIEnv *env_;
    jobjectArray local_ref_;
    size_t size_;
    LSPLANT_DISALLOW_COPY_AND_ASSIGN(ScopedLocalRef);
};
// functions to array

template <ScopeOrRaw<jarray> Array>
[[maybe_unused]] inline auto JNI_GetArrayLength(JNIEnv *env, const Array &array) {
    return JNI_Invoke(env, &JNIEnv::GetArrayLength, array);
}

// newers

template <ScopeOrClass Class, ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_NewObjectArray(JNIEnv *env, jsize len, Class &&clazz,
                                                const Object &init) {
    return JNI_SafeInvoke(env, &JNIEnv::NewObjectArray, len, std::forward<Class>(clazz), init);
}

[[maybe_unused]] inline auto JNI_NewBooleanArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewBooleanArray, len);
}

[[maybe_unused]] inline auto JNI_NewByteArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewByteArray, len);
}

[[maybe_unused]] inline auto JNI_NewCharArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewCharArray, len);
}

[[maybe_unused]] inline auto JNI_NewShortArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewShortArray, len);
}

[[maybe_unused]] inline auto JNI_NewIntArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewIntArray, len);
}

[[maybe_unused]] inline auto JNI_NewLongArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewLongArray, len);
}

[[maybe_unused]] inline auto JNI_NewFloatArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewFloatArray, len);
}

[[maybe_unused]] inline auto JNI_NewDoubleArray(JNIEnv *env, jsize len) {
    return JNI_Invoke(env, &JNIEnv::NewDoubleArray, len);
}

template <ScopeOrJCompatiblePointer Type>
[[maybe_unused]] inline auto JNI_NewArray(JNIEnv *env, jsize len) {
    if constexpr (std::is_same_v<Type, jboolean>) {
        return JNI_NewBooleanArray(env, len);
    } else if constexpr (std::is_same_v<Type, jbyte>) {
        return JNI_NewByteArray(env, len);
    } else if constexpr (std::is_same_v<Type, jshort>) {
        return JNI_NewShortArray(env, len);
    } else if constexpr (std::is_same_v<Type, jchar>) {
        return JNI_NewCharArray(env, len);
    } else if constexpr (std::is_same_v<Type, jint> ||
                         (std::is_pointer_v<Type> && sizeof(Type) == sizeof(jint))) {
        return JNI_NewIntArray(env, len);
    } else if constexpr (std::is_same_v<Type, jfloat>) {
        return JNI_NewFloatArray(env, len);
    } else if constexpr (std::is_same_v<Type, jlong> ||
                         (std::is_pointer_v<Type> && sizeof(Type) == sizeof(jlong))) {
        return JNI_NewLongArray(env, len);
    } else if constexpr (std::is_same_v<Type, jdouble>) {
        return JNI_NewDoubleArray(env, len);
    } else {
        std::unreachable();
    }
}

template <ScopeOrObject Object>
[[maybe_unused]] inline auto JNI_GetObjectFieldOf(JNIEnv *env, Object &&object,
                                                  std::string_view field_name,
                                                  std::string_view field_class) {
    auto &&o = std::forward<Object>(object);
    return JNI_GetObjectField(
        env, o, JNI_GetFieldID(env, JNI_GetObjectClass(env, o), field_name, field_class));
}

template <typename T = jbyte>
class JDirectBuffer {
public:
    JDirectBuffer(const ScopedLocalRef<jobject> &buffer)
        : JDirectBuffer(buffer.env_, buffer.local_ref_, nullptr) {}

    JDirectBuffer(JNIEnv *env, jobject buffer) : env_(env), buffer_(buffer) {
        if (env && buffer) {
            address_ = static_cast<T *>(env->GetDirectBufferAddress(buffer));
            size_ = static_cast<size_t>(env->GetDirectBufferCapacity(buffer));
        }
    }

    operator const T *() const { return address_; }

    operator T *() { return address_; }

    operator const bool() const { return address_ != nullptr; }

    const auto &operator*() const { return *address_; }

    auto &operator*() { return *address_; }

    const auto *operator->() const { return address_; }

    auto *operator->() { return address_; }

    const auto &operator[](size_t index) const { return address_[index]; }

    auto &operator[](size_t index) { return address_[index]; }

    auto get() const { return buffer_; }

    const auto *data() const { return address_; }

    auto *data() { return address_; }

    auto size() const { return size_; }

    auto length() const { return size_ / sizeof(T); }

private:
    JDirectBuffer(JNIEnv *env, jobject buffer, T *address, size_t size)
        : env_(env), buffer_(buffer), address_(address), size_(size) {}

    JNIEnv *env_;
    jobject buffer_;
    T *address_;
    size_t size_;
};

class JBasicObjectCollection {
protected:
    JBasicObjectCollection() = default;

    static void arraycopy(JNIEnv *env, jarray src, jsize src_pos, jarray dest, jsize dest_pos,
                          jsize len) {
        auto system_cls = get_system_class(env);
        auto mid = get_system_arraycopy_method(env, system_cls);
        env->CallStaticVoidMethod(system_cls, mid, src, src_pos, dest, dest_pos, len);
    }

    static jclass get_object_class(JNIEnv *env) {
        if (!object_class_ || env->IsSameObject(object_class_, nullptr)) {
            auto clazz = env->FindClass("java/lang/Object");
            object_class_ = env->NewWeakGlobalRef(clazz);
            env->DeleteLocalRef(clazz);
        }
        return reinterpret_cast<jclass>(object_class_);
    }

    static jclass get_system_class(JNIEnv *env) {
        if (!system_class_ || env->IsSameObject(system_class_, nullptr)) {
            auto clazz = env->FindClass("java/lang/System");
            system_class_ = env->NewWeakGlobalRef(clazz);
            env->DeleteLocalRef(clazz);
        }
        return reinterpret_cast<jclass>(system_class_);
    }

    static jmethodID get_system_arraycopy_method(JNIEnv *env, jclass system_class) {
        if (!system_arraycopy_method_) {
            system_arraycopy_method_ = env->GetStaticMethodID(
                system_class, "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V");
        }
        return system_arraycopy_method_;
    }

    static void delete_weak_globals(JNIEnv *env) {
        if (object_class_) [[likely]] {
            env->DeleteWeakGlobalRef(object_class_);
            object_class_ = nullptr;
        }
        if (system_class_) [[likely]] {
            env->DeleteWeakGlobalRef(system_class_);
            system_class_ = nullptr;
        }
    }

private:
    inline static jweak object_class_{};
    inline static jweak system_class_{};
    inline static jmethodID system_arraycopy_method_{};
};

template <bool kIsGlobalReference>
class JBasicObjectList : private JBasicObjectCollection {
public:
    class Iterator {
    public:
        Iterator(JNIEnv *env, jobjectArray array, jsize index)
            : env_(env), array_(array), index_(index) {}

        jobject operator*() const { return env_->GetObjectArrayElement(array_, index_); }

        Iterator &operator++() {
            ++index_;
            return *this;
        }

        bool operator!=(const Iterator &other) const { return index_ != other.index_; }

    private:
        JNIEnv *env_;
        jobjectArray array_;
        jsize index_;
    };

    JBasicObjectList() = default;

    JBasicObjectList(const JBasicObjectList &other) = delete;
    JBasicObjectList &operator=(const JBasicObjectList &other) = delete;

    JBasicObjectList(JBasicObjectList &&other) noexcept { move_from(std::move(other)); }
    JBasicObjectList &operator=(JBasicObjectList &&other) noexcept {
        if (this != &other) {
            move_from(std::move(other));
        }
        return *this;
    }

    jobjectArray get_array() const { return array_; }
    jsize size() const { return length_ - available_; }
    jsize capacity() const { return length_; }

    Iterator begin(JNIEnv *env) const { return Iterator{env, array_, 0}; }
    Iterator end() const { return Iterator{nullptr, nullptr, length_}; }

    void reserve(JNIEnv *env, jsize new_capacity) {
        if (new_capacity <= length_) return;
        grow_to(env, new_capacity);
    }

    jsize add(JNIEnv *env, jobject obj) {
        if (!obj) return -1;

        if (available_ == 0) {
            auto growth = length_ == 0 ? 16 : std::min<jsize>(length_, 128);
            grow_to(env, length_ + growth);
        }

        auto start = cursor_;
        for (jsize i = 0; i < length_; ++i) {
            auto idx = (start + i) % length_;
            auto cur = env->GetObjectArrayElement(array_, idx);
            if (cur) {
                env->DeleteLocalRef(cur);
                continue;
            }
            env->SetObjectArrayElement(array_, idx, obj);
            --available_;
            cursor_ = (idx + 1) % length_;
            return idx;
        }

        std::unreachable();
    }

    jobject get(JNIEnv *env, jsize index) const {
        if (index < 0 || index >= length_) return nullptr;
        return env->GetObjectArrayElement(array_, index);
    }

    void update(JNIEnv *env, jsize index, jobject obj) {
        if (index < 0 || index >= length_) return;
        env->SetObjectArrayElement(array_, index, obj);
    }

    void remove(JNIEnv *env, jsize index) {
        if (index < 0 || index >= length_) return;
        env->SetObjectArrayElement(array_, index, nullptr);
        ++available_;
        cursor_ = index;
    }

    void finalize(JNIEnv *env) {
        if (!array_ || !length_ || !available_) return;

        jsize last_non_null = -1;
        for (jsize i = length_ - 1; i >= 0; --i) {
            auto obj = env->GetObjectArrayElement(array_, i);
            if (!obj) continue;
            env->DeleteLocalRef(obj);
            last_non_null = i;
            break;
        }

        auto new_length = last_non_null + 1;
        if (new_length == length_) return;

        auto object_cls = get_object_class(env);
        auto new_array = env->NewObjectArray(new_length, object_cls, nullptr);

        if (new_length > 0) {
            arraycopy(env, array_, 0, new_array, 0, new_length);
        }

        if constexpr (kIsGlobalReference) {
            env->DeleteGlobalRef(array_);
            array_ = reinterpret_cast<jobjectArray>(env->NewGlobalRef(new_array));
            env->DeleteLocalRef(new_array);
        } else {
            env->DeleteLocalRef(array_);
            array_ = new_array;
        }

        length_ = new_length;
        available_ = 0;
        cursor_ = new_length == 0 ? 0 : new_length - 1;

        if constexpr (kIsGlobalReference) {
            delete_weak_globals(env);
        }
    }

    void clear(JNIEnv *env) {
        if (!array_) return;

        for (jsize i = 0; i < length_; ++i) {
            env->SetObjectArrayElement(array_, i, nullptr);
        }
        cursor_ = 0;
        available_ = length_;
    }

    void recycle(JNIEnv *env) {
        if (array_) {
            if constexpr (kIsGlobalReference) {
                env->DeleteGlobalRef(array_);
            } else {
                env->DeleteLocalRef(array_);
            }
        }
        array_ = nullptr;
        length_ = available_ = cursor_ = 0;
    }

private:
    void move_from(JBasicObjectList &&other) {
        array_ = other.array_;
        cursor_ = other.cursor_;
        available_ = other.available_;
        length_ = other.length_;
        other.array_ = nullptr;
        other.cursor_ = other.available_ = other.length_ = 0;
    }

    void grow_to(JNIEnv *env, jsize new_length) {
        if (!array_) {
            allocate_array(env, new_length);
            return;
        }

        auto new_array = allocate_local(env, new_length);
        arraycopy(env, array_, 0, new_array, 0, length_);

        if constexpr (kIsGlobalReference) {
            env->DeleteGlobalRef(array_);
            array_ = reinterpret_cast<jobjectArray>(env->NewGlobalRef(new_array));
            env->DeleteLocalRef(new_array);
        } else {
            env->DeleteLocalRef(array_);
            array_ = new_array;
        }

        available_ += (new_length - length_);
        length_ = new_length;
    }

    void allocate_array(JNIEnv *env, jsize n) {
        auto arr = allocate_local(env, n);
        if constexpr (kIsGlobalReference) {
            array_ = reinterpret_cast<jobjectArray>(env->NewGlobalRef(arr));
            env->DeleteLocalRef(arr);
        } else {
            array_ = arr;
        }
        length_ = n;
        available_ = n;
        cursor_ = 0;
    }

    static jobjectArray allocate_local(JNIEnv *env, jsize n) {
        auto object_cls = get_object_class(env);
        return env->NewObjectArray(n, object_cls, nullptr);
    }

    jobjectArray array_{};
    jsize cursor_{};
    jsize available_{};
    jsize length_{};
};

template <bool kIsGlobalReference>
class JBasicObjectMap : private JBasicObjectCollection {
public:
    JBasicObjectMap() = default;

    JBasicObjectMap(const JBasicObjectMap &other) = delete;
    JBasicObjectMap &operator=(const JBasicObjectMap &other) = delete;

    JBasicObjectMap(JBasicObjectMap &&other) noexcept { move_from(std::move(other)); }
    JBasicObjectMap &operator=(JBasicObjectMap &&other) noexcept {
        if (this != &other) {
            move_from(std::move(other));
        }
        return *this;
    }

    jobjectArray get_keys_array() const { return keys_; }
    jobjectArray get_values_array() const { return values_; }
    jsize size() const { return length_ - available_; }
    jsize capacity() const { return length_; }

    void reserve(JNIEnv *env, jsize new_capacity) {
        if (new_capacity <= length_) return;
        grow_to(env, new_capacity);
    }

    bool add(JNIEnv *env, jobject key, jobject value) {
        if (!key) return false;

        for (jsize i = 0; i < length_; ++i) {
            auto current_key = env->GetObjectArrayElement(keys_, i);
            if (!current_key) continue;

            bool is_same = env->IsSameObject(current_key, key);
            env->DeleteLocalRef(current_key);

            if (is_same) {
                env->SetObjectArrayElement(values_, i, value);
                return true;
            }
        }

        if (available_ == 0) {
            auto growth = length_ == 0 ? 16 : std::min<jsize>(length_, 128);
            grow_to(env, length_ + growth);
        }

        auto start = cursor_;
        for (jsize i = 0; i < length_; ++i) {
            auto idx = (start + i) % length_;
            auto current_key = env->GetObjectArrayElement(keys_, idx);

            if (current_key) {
                env->DeleteLocalRef(current_key);
                continue;
            }

            env->SetObjectArrayElement(keys_, idx, key);
            env->SetObjectArrayElement(values_, idx, value);

            --available_;
            cursor_ = (idx + 1) % length_;
            return true;
        }

        std::unreachable();
    }

    jobject get(JNIEnv *env, jobject key) const {
        if (!key || !keys_) return nullptr;

        for (jsize i = 0; i < length_; ++i) {
            auto current_key = env->GetObjectArrayElement(keys_, i);
            if (!current_key) continue;

            bool is_same = env->IsSameObject(current_key, key);
            env->DeleteLocalRef(current_key);

            if (is_same) {
                return env->GetObjectArrayElement(values_, i);
            }
        }
        return nullptr;
    }

    void remove(JNIEnv *env, jobject key) {
        if (!key || !keys_) return;

        for (jsize i = 0; i < length_; ++i) {
            auto current_key = env->GetObjectArrayElement(keys_, i);
            if (!current_key) continue;

            bool is_same = env->IsSameObject(current_key, key);
            env->DeleteLocalRef(current_key);

            if (is_same) {
                env->SetObjectArrayElement(keys_, i, nullptr);
                env->SetObjectArrayElement(values_, i, nullptr);
                ++available_;
                cursor_ = i;
                return;
            }
        }
    }

    void finalize(JNIEnv *env) {
        if (!keys_ || !length_ || !available_) return;

        jsize real_count = length_ - available_;
        if (real_count == 0) {
            clear(env);
            return;
        }

        if (real_count == length_) return;

        auto object_cls = get_object_class(env);
        auto new_keys = env->NewObjectArray(real_count, object_cls, nullptr);
        auto new_values = env->NewObjectArray(real_count, object_cls, nullptr);

        jsize dest_idx = 0;
        for (jsize i = 0; i < length_; ++i) {
            auto k = env->GetObjectArrayElement(keys_, i);
            if (k) {
                auto v = env->GetObjectArrayElement(values_, i);

                env->SetObjectArrayElement(new_keys, dest_idx, k);
                env->SetObjectArrayElement(new_values, dest_idx, v);

                env->DeleteLocalRef(k);
                if (v) env->DeleteLocalRef(v);

                dest_idx++;
            }
        }

        replace_arrays(env, new_keys, new_values);

        length_ = real_count;
        available_ = 0;
        cursor_ = 0;

        if constexpr (kIsGlobalReference) {
            delete_weak_globals(env);
        }
    }

    void clear(JNIEnv *env) {
        if (!keys_) return;

        for (jsize i = 0; i < length_; ++i) {
            env->SetObjectArrayElement(keys_, i, nullptr);
            env->SetObjectArrayElement(values_, i, nullptr);
        }
        cursor_ = 0;
        available_ = length_;
    }

    void recycle(JNIEnv *env) {
        if (keys_) {
            if constexpr (kIsGlobalReference) {
                env->DeleteGlobalRef(keys_);
                env->DeleteGlobalRef(values_);
            } else {
                env->DeleteLocalRef(keys_);
                env->DeleteLocalRef(values_);
            }
        }
        keys_ = nullptr;
        values_ = nullptr;
        length_ = available_ = cursor_ = 0;
    }

private:
    void move_from(JBasicObjectMap &&other) {
        keys_ = other.keys_;
        values_ = other.values_;
        cursor_ = other.cursor_;
        available_ = other.available_;
        length_ = other.length_;

        other.keys_ = nullptr;
        other.values_ = nullptr;
        other.cursor_ = other.available_ = other.length_ = 0;
    }

    void replace_arrays(JNIEnv *env, jobjectArray new_keys_local, jobjectArray new_values_local) {
        if constexpr (kIsGlobalReference) {
            if (keys_) env->DeleteGlobalRef(keys_);
            if (values_) env->DeleteGlobalRef(values_);

            keys_ = reinterpret_cast<jobjectArray>(env->NewGlobalRef(new_keys_local));
            values_ = reinterpret_cast<jobjectArray>(env->NewGlobalRef(new_values_local));

            env->DeleteLocalRef(new_keys_local);
            env->DeleteLocalRef(new_values_local);
        } else {
            if (keys_) env->DeleteLocalRef(keys_);
            if (values_) env->DeleteLocalRef(values_);

            keys_ = new_keys_local;
            values_ = new_values_local;
        }
    }

    void grow_to(JNIEnv *env, jsize new_length) {
        if (!keys_) {
            allocate_arrays(env, new_length);
            return;
        }

        auto object_cls = get_object_class(env);
        auto new_keys = env->NewObjectArray(new_length, object_cls, nullptr);
        auto new_values = env->NewObjectArray(new_length, object_cls, nullptr);

        arraycopy(env, keys_, 0, new_keys, 0, length_);
        arraycopy(env, values_, 0, new_values, 0, length_);

        replace_arrays(env, new_keys, new_values);

        available_ += (new_length - length_);
        length_ = new_length;
    }

    void allocate_arrays(JNIEnv *env, jsize n) {
        auto object_cls = get_object_class(env);
        auto k_arr = env->NewObjectArray(n, object_cls, nullptr);
        auto v_arr = env->NewObjectArray(n, object_cls, nullptr);

        replace_arrays(env, k_arr, v_arr);

        length_ = n;
        available_ = n;
        cursor_ = 0;
    }

    jobjectArray keys_{};
    jobjectArray values_{};
    jsize cursor_{};
    jsize available_{};
    jsize length_{};
};

using JObjectList = JBasicObjectList<true>;
using JLocalObjectList = JBasicObjectList<false>;

using JObjectMap = JBasicObjectMap<true>;
using JLocalObjectMap = JBasicObjectMap<false>;

}  // namespace lsplant

#undef LSPLANT_DISALLOW_COPY_AND_ASSIGN

#pragma clang diagnostic pop
