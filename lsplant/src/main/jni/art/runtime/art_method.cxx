module;

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:art_method;

import :common;
import :reflection;
import :unsafe;
import :runtime;
import :jni_id_manager;
#endif

export namespace lsplant::art {

struct alignas(4) [[gnu::packed]] QuickMethodFrameInfo {
    [[maybe_unused]] uint32_t frame_size_in_bytes;
    [[maybe_unused]] uint32_t core_spill_mask;
    [[maybe_unused]] uint32_t fp_spill_mask;
};

class ArtMethod {
    inline static ArtMethod *abstract_method_{};

    inline static auto PrettyMethod_ =
        "_ZN3art9ArtMethod12PrettyMethodEPS0_b"_sym.as<std::string (ArtMethod::*)(bool)>;

    inline static auto PrettyMethodStatic_ =
        "_ZN3art12PrettyMethodEPNS_9ArtMethodEb"_sym
            .as<std::string(ArtMethod *thiz, bool with_signature)>;

    inline static auto PrettyMethodMirror_ =
        "_ZN3art12PrettyMethodEPNS_6mirror9ArtMethodEb"_sym
            .as<std::string(ArtMethod *thiz, bool with_signature)>;

    inline static auto GetShorty_ =
        "_ZN3art9ArtMethod9GetShortyEPj"_sym.as<const char *(ArtMethod::*)(uint32_t *out_length)>;

    inline static auto GetMethodShortyL_ = "_ZN3artL15GetMethodShortyEP7_JNIEnvP10_jmethodID"_sym
                                               .as<const char *(JNIEnv *env, jmethodID method)>;

    inline static auto GetMethodShorty_ = "_ZN3art15GetMethodShortyEP7_JNIEnvP10_jmethodID"_sym
                                              .as<const char *(JNIEnv *env, jmethodID mid)>;

    inline static auto SetNotIntrinsic_ =
        "_ZN3art9ArtMethod15SetNotIntrinsicEv"_sym.as<void (ArtMethod::*)()>;

    inline static auto ThrowInvocationTimeError_ =
        "_ZN3art9ArtMethod24ThrowInvocationTimeErrorEv"_sym.as<void (ArtMethod::*)()>;

    inline static auto art_interpreter_to_compiled_code_bridge_ =
        "artInterpreterToCompiledCodeBridge"_sym.as<void()>;

    inline static auto GetQuickFrameInfo_ =
        "_ZN3art9ArtMethod17GetQuickFrameInfoEv"_sym.hook->*
        []<MemBackup auto backup>(ArtMethod *thiz) static -> QuickMethodFrameInfo {
        if (backuped_proxy_methods_.contains(thiz)) [[unlikely]] {
            return backup(abstract_method_);
        }
        return backup(thiz);
    };

    template <bool kIsStatic>
    static art::ArtMethod *FindMethod(JNIEnv *env, jclass clazz,
                                      const std::string_view &method_name,
                                      const std::string_view &signature) {
        auto method_id = kIsStatic ? JNI_GetStaticMethodID(env, clazz, method_name, signature)
                                   : JNI_GetMethodID(env, clazz, method_name, signature);
        return FromJMethodID(env, clazz, method_id, kIsStatic);
    }

public:
    jmethodID ToJMethodID() {
        // There may be a better way
        return reinterpret_cast<jmethodID>(this);
    }

    std::string_view GetShorty(JNIEnv *env) {
        if (GetShorty_) [[likely]] {
            uint32_t out_length;
            return {GetShorty_(this, &out_length), static_cast<size_t>(out_length)};
        } else if (GetMethodShortyL_) {
            return GetMethodShortyL_(env, ToJMethodID());
        } else if (GetMethodShorty_) {
            return GetMethodShorty_(env, ToJMethodID());
        }
        return {};
    }

    void SetNonCompilable() {
        auto access_flags = GetAccessFlags();
        access_flags |= kAccCompileDontBother;
        access_flags &= ~kAccPreCompiled;
        SetAccessFlags(access_flags);
    }

    void ClearFastInterpretFlag() {
        auto access_flags = GetAccessFlags();
        access_flags &= ~kAccFastInterpreterToInterpreterInvoke;
        SetAccessFlags(access_flags);
    }

    void SetPrivate() {
        auto access_flags = GetAccessFlags();
        access_flags |= kAccPrivate;
        access_flags &= ~kAccProtected;
        access_flags &= ~kAccPublic;
        SetAccessFlags(access_flags);
    }

    void SetPublic() {
        auto access_flags = GetAccessFlags();
        access_flags |= kAccPublic;
        access_flags &= ~kAccProtected;
        access_flags &= ~kAccPrivate;
        SetAccessFlags(access_flags);
    }

    void SetProtected() {
        auto access_flags = GetAccessFlags();
        access_flags |= kAccProtected;
        access_flags &= ~kAccPrivate;
        access_flags &= ~kAccPublic;
        SetAccessFlags(access_flags);
    }

    void SetNonFinal() {
        auto access_flags = GetAccessFlags();
        access_flags &= ~kAccFinal;
        SetAccessFlags(access_flags);
    }

    void SetNative() {
        auto access_flags = GetAccessFlags();
        access_flags |= kAccNative;
        SetAccessFlags(access_flags);
    }

    void SetNonNative() {
        auto access_flags = GetAccessFlags();
        access_flags &= ~kAccNative;
        SetAccessFlags(access_flags);
    }

    void SetFastNative() {
        auto access_flags = GetAccessFlags();
        access_flags |= kAccFastNative;
        SetAccessFlags(access_flags);
    }

    void SetNonFastNative() {
        auto access_flags = GetAccessFlags();
        access_flags &= ~kAccFastNative;
        SetAccessFlags(access_flags);
    }

    void SetConstructor() {
        auto access_flags = GetAccessFlags();
        access_flags |= kAccConstructor;
        SetAccessFlags(access_flags);
    }

    void SetNonConstructor() {
        auto access_flags = GetAccessFlags();
        access_flags &= ~kAccConstructor;
        SetAccessFlags(access_flags);
    }

    void SetNonIntrinsic() {
        if (SetNotIntrinsic_) [[likely]] {
            SetNotIntrinsic_(this);
            return;
        }
        auto access_flags = GetAccessFlags();
        access_flags &= ~kAccIntrinsic;
        SetAccessFlags(access_flags);
    }

    bool IsPrivate() { return GetAccessFlags() & kAccPrivate; }
    bool IsProtected() { return GetAccessFlags() & kAccProtected; }
    bool IsPublic() { return GetAccessFlags() & kAccPublic; }
    bool IsFinal() { return GetAccessFlags() & kAccFinal; }
    bool IsStatic() { return GetAccessFlags() & kAccStatic; }
    bool IsNative() { return GetAccessFlags() & kAccNative; }
    bool IsFastNative() { return GetAccessFlags() & kAccFastNative && IsNative(); }
    bool IsAbstract() { return GetAccessFlags() & kAccAbstract; }
    bool IsConstructor() { return GetAccessFlags() & kAccConstructor; }

    void CopyFrom(const ArtMethod *other) { std::memcpy(this, other, art_method_size); }

    void SetEntryPoint(void *entry_point) {
        *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this) + entry_point_offset) =
            entry_point;
        if (interpreter_entry_point_offset) [[unlikely]] {
            *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this) +
                                       interpreter_entry_point_offset) =
                reinterpret_cast<void *>(&art_interpreter_to_compiled_code_bridge_);
        }
    }

    void *GetEntryPoint() {
        return *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this) + entry_point_offset);
    }

    void *GetData() {
        return *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this) + data_offset);
    }

    void SetData(void *data) {
        *reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(this) + data_offset) = data;
    }

    uint32_t GetAccessFlags() {
        return (reinterpret_cast<const std::atomic<uint32_t> *>(reinterpret_cast<uintptr_t>(this) +
                                                                access_flags_offset))
            ->load(std::memory_order_relaxed);
    }

    void SetAccessFlags(uint32_t flags) {
        reinterpret_cast<std::atomic<uint32_t> *>(reinterpret_cast<uintptr_t>(this) +
                                                  access_flags_offset)
            ->store(flags, std::memory_order_relaxed);
    }

    std::string PrettyMethod(bool with_signature = true) {
        if (PrettyMethod_) [[likely]] {
            return PrettyMethod_(this, with_signature);
        }
        if (PrettyMethodStatic_) {
            return PrettyMethodStatic_(this, with_signature);
        }
        if (PrettyMethodMirror_) {
            return PrettyMethodMirror_(this, with_signature);
        }
        return "null sym";
    }

    mirror::Class *GetDeclaringClass() {
        return reinterpret_cast<mirror::Class *>(*reinterpret_cast<uint32_t *>(
            reinterpret_cast<uintptr_t>(this) + declaring_class_offset));
    }

    ArtMethod *GetInterfaceMethodIfProxy() {
        if (GetAndroidApiLevel() >= __ANDROID_API_P__) [[likely]] {
            return static_cast<ArtMethod *>(GetData());
        }
        return nullptr;
    }

    [[nodiscard]] std::unique_ptr<ArtMethod> Clone() const {
        auto *method = reinterpret_cast<ArtMethod *>(::operator new(art_method_size));
        method->CopyFrom(this);
        return std::unique_ptr<ArtMethod>(method);
    }

    void BackupTo(ArtMethod *backup) {
        SetNonCompilable();

        // copy after setNonCompilable
        backup->CopyFrom(this);

        ClearFastInterpretFlag();

        if (!backup->IsStatic()) backup->SetPrivate();
    }

    template <FixedString Sym, typename... Us, template <FixedString, typename...> typename T>
        requires(requires { T<Sym, Us...>::replace_; })
    bool NativeHook(T<Sym, Us...> &hooker) {
        if (!IsNative()) [[unlikely]] {
            return false;
        }
        hooker = GetData();
        SetData(reinterpret_cast<void *>(hooker.replace_));
        hooker.handle_ = this;
        return true;
    }

    template <FixedString Sym, typename... Us, template <FixedString, typename...> typename T>
        requires(requires { T<Sym, Us...>::replace_; })
    static bool NativeUnhook(T<Sym, Us...> &hooker) {
        auto method = static_cast<ArtMethod *>(hooker.handle_);
        if (!method || !method->IsNative()) [[unlikely]] {
            return false;
        }
        method->SetData(reinterpret_cast<void *>(&hooker));
        hooker.handle_ = nullptr;
        return true;
    }

    static art::ArtMethod *FromReflectedMethod(JNIEnv *env, jobject method) {
        if (art_method_field) [[likely]] {
            return JNI_GetLongField<art::ArtMethod *>(env, method, art_method_field);
        } else {
            return reinterpret_cast<art::ArtMethod *>(env->FromReflectedMethod(method));
        }
    }

    static art::ArtMethod *FromJMethodID(JNIEnv *env, jclass clazz, jmethodID method_id,
                                         bool is_static) {
        if (GetAndroidApiLevel() < __ANDROID_API_R__ || !jni::JniIdManager::IsIndexId(method_id))
            [[unlikely]] {
            return reinterpret_cast<ArtMethod *>(method_id);
        }
        if (auto decoder = Runtime::Current()->GetJniIdManager()) {
            auto method = decoder->DecodeMethodId(method_id);
            if (method) [[likely]] {
                return method;
            }
        }
        auto reflected_method = JNI_ToReflectedMethod(env, clazz, method_id, is_static);
        return FromReflectedMethod(env, reflected_method.get());
    }

    static art::ArtMethod *FindStaticMethod(JNIEnv *env, jclass clazz,
                                            const std::string_view &method_name,
                                            const std::string_view &signature) {
        return FindMethod<true>(env, clazz, method_name, signature);
    }

    static art::ArtMethod *FindMethod(JNIEnv *env, jclass clazz,
                                      const std::string_view &method_name,
                                      const std::string_view &signature) {
        return FindMethod<false>(env, clazz, method_name, signature);
    }

    static bool Init(JNIEnv *env, const HookHandler handler) {
        auto sdk_int = GetAndroidApiLevel();

        auto executable = executable_ref.get(env);
        if (sdk_int < __ANDROID_API_M__) [[unlikely]] {
            executable = JNI_FindClass(env, "java/lang/reflect/ArtMethod");
            if (!executable) [[unlikely]] {
                LOGE("Failed to find ArtMethod.class");
                return false;
            }
        }

        if (sdk_int >= __ANDROID_API_M__) [[likely]] {
            if (art_method_field = JNI_GetFieldID(env, executable, "artMethod", "J");
                !art_method_field) [[unlikely]] {
                LOGE("Failed to find Executable.artMethod");
                return false;
            }
        }

        auto throwable = JNI_FindClass(env, "java/lang/Throwable");
        if (!throwable) [[unlikely]] {
            LOGE("Failed to find Throwable.class");
            return false;
        }
        const auto constructors =
            JNI_CallObjectMethod<jobjectArray>(env, throwable, class_get_declared_constructors);
        if (constructors.size() < 2) [[unlikely]] {
            LOGE("Throwable has less than 2 constructors");
            return false;
        }
        auto first_ctor = constructors[0];
        auto second_ctor = constructors[1];
        auto *first = FromReflectedMethod(env, first_ctor.get());
        auto *second = FromReflectedMethod(env, second_ctor.get());
        art_method_size = reinterpret_cast<uintptr_t>(second) - reinterpret_cast<uintptr_t>(first);
        LOGD("ArtMethod size: %zu", art_method_size);

        if (RoundUpTo(4 * 9, kPointerSize) + kPointerSize * 3 < art_method_size) [[unlikely]] {
            if (sdk_int >= __ANDROID_API_M__) {
                LOGW("ArtMethod size exceeds maximum assume; There may be something wrong.");
            }
        }

        entry_point_offset = art_method_size - kPointerSize;
        data_offset = entry_point_offset - kPointerSize;

        if (sdk_int >= __ANDROID_API_M__) [[likely]] {
            uint32_t real_flags = JNI_GetIntField(env, first_ctor, method_access_flags_field);
            for (size_t i = 0; i < art_method_size; i += sizeof(uint32_t)) {
                if (*reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(first) + i) ==
                    real_flags) {
                    access_flags_offset = i;
                    break;
                }
            }
            if (access_flags_offset == 0) [[unlikely]] {
                if (sdk_int == __ANDROID_API_M__) [[unlikely]] {
                    access_flags_offset = 12U;
                } else {
                    access_flags_offset = 4U;
                }
                LOGW("Failed to find ArtMethod::access_flags; Fallback to %zu.",
                     access_flags_offset);
            }
        } else if (auto unsafe = mirror::Unsafe::GetUnsafe(env);
                   unsafe && unsafe.HasObjectFieldOffset()) [[likely]] {
            auto get_offset_from_art_method = [&, env](const char *name,
                                                       const char *sig) -> size_t {
                auto field = JNI_GetFieldID(env, executable, name, sig);
                if (!field) [[unlikely]] {
                    LOGW("Failed to find ArtMethod.%s", name);
                    return 0u;
                }
                return unsafe.ObjectFieldOffset(
                    JNI_ToReflectedField(env, executable, field, false).get());
            };
            access_flags_offset = get_offset_from_art_method("accessFlags", "I");
            declaring_class_offset =
                get_offset_from_art_method("declaringClass", "Ljava/lang/Class;");
            if (sdk_int == __ANDROID_API_L__) {
                entry_point_offset =
                    get_offset_from_art_method("entryPointFromQuickCompiledCode", "J");
                interpreter_entry_point_offset =
                    get_offset_from_art_method("entryPointFromInterpreter", "J");
                data_offset = get_offset_from_art_method("entryPointFromJni", "J");
            }
        } else {
            LOGW("Unsafe is not available");
            return false;
        }
        LOGD("ArtMethod::declaring_class offset: %zu", declaring_class_offset);
        LOGD("ArtMethod::access_flags    offset: %zu", access_flags_offset);
        LOGD("ArtMethod::data            offset: %zu", data_offset);
        LOGD("ArtMethod::entrypoint      offset: %zu", entry_point_offset);

        if (sdk_int < __ANDROID_API_R__) [[unlikely]] {
            kAccPreCompiled = 0;
        } else if (sdk_int >= __ANDROID_API_S__) [[likely]] {
            kAccPreCompiled = 0x00800000;
        }
        if (sdk_int < __ANDROID_API_Q__) [[unlikely]] {
            kAccFastInterpreterToInterpreterInvoke = 0;
        }
        if (sdk_int < __ANDROID_API_O__) [[unlikely]] {
            kAccFastNative = 0;
            kAccIntrinsic = 0;
        }

        if (!handler(GetShorty_, GetMethodShortyL_, true, GetMethodShorty_)) {
            LOGW("Failed to find GetMethodShorty");
        }

        handler(PrettyMethod_, PrettyMethodStatic_, PrettyMethodMirror_);

        if (sdk_int >= __ANDROID_API_P__ && !handler(SetNotIntrinsic_)) {
            LOGW("Failed to find SetNotIntrinsic, use hard-coded kAccIntrinsic instead");
        }

        if (sdk_int == __ANDROID_API_O__) [[unlikely]] {
            auto abstract_method_error = JNI_FindClass(env, "java/lang/AbstractMethodError");
            if (!abstract_method_error) [[unlikely]] {
                LOGE("Failed to find AbstractMethodError.class");
                return false;
            }

            auto abstract_method = FromJMethodID(env, executable.get(), method_get_name, false);
            uint32_t access_flags = abstract_method->GetAccessFlags();
            abstract_method->SetAccessFlags(access_flags | kAccDefaultConflict);
            if (handler(ThrowInvocationTimeError_)) [[likely]] {
                ThrowInvocationTimeError_(abstract_method);
            }
            abstract_method->SetAccessFlags(access_flags);

            if (auto exception = env->ExceptionOccurred();
                env->ExceptionClear(),
                (!exception || JNI_IsInstanceOf(env, exception, abstract_method_error)))
                [[likely]] {
                kAccCompileDontBother = kAccDefaultConflict;
            } else {
                LOGW("Detected android 8.1 runtime on android 8.0 device");
            }
        }
        if (sdk_int == __ANDROID_API_M__) [[unlikely]] {
            abstract_method_ = FromJMethodID(env, executable.get(), method_get_name, false);
            if (!abstract_method_ || !abstract_method_->IsAbstract()) [[unlikely]] {
                LOGW("Abstract method Executable.getName not found");
            } else if (!handler(GetQuickFrameInfo_)) [[unlikely]] {
                LOGW("Failed to hook GetQuickFrameInfo, hooking proxy method may crash");
            }
        }
        if (sdk_int < __ANDROID_API_N__) {
            kAccCompileDontBother = 0;
        }
        if (sdk_int <= __ANDROID_API_M__) [[unlikely]] {
            if (!handler(art_interpreter_to_compiled_code_bridge_)) {
                LOGE("Failed to find artInterpreterToCompiledCodeBridge");
                return false;
            }
            if (sdk_int >= __ANDROID_API_L_MR1__) {
                interpreter_entry_point_offset = entry_point_offset - 2 * kPointerSize;
            }
        }

        return true;
    }

    static size_t GetArtMethodSize() { return art_method_size; }

    static size_t GetEntryPointOffset() { return entry_point_offset; }

    static bool CanGetMethodShorty() { return GetShorty_ || GetMethodShortyL_ || GetMethodShorty_; }

    constexpr static uint32_t kAccPublic = 0x0001;           // class, field, method, ic
    constexpr static uint32_t kAccPrivate = 0x0002;          // field, method, ic
    constexpr static uint32_t kAccProtected = 0x0004;        // field, method, ic
    constexpr static uint32_t kAccStatic = 0x0008;           // field, method, ic
    constexpr static uint32_t kAccNative = 0x0100;           // method
    constexpr static uint32_t kAccFinal = 0x0010;            // class, field, method, ic
    constexpr static uint32_t kAccAbstract = 0x0400;         // class, method, ic
    constexpr static uint32_t kAccConstructor = 0x00010000;  // method (dex only) <(cl)init>

private:
    inline static jfieldID art_method_field{};
    inline static size_t art_method_size{};
    inline static size_t entry_point_offset{};
    inline static size_t interpreter_entry_point_offset{};
    inline static size_t data_offset{};
    inline static size_t access_flags_offset{};
    inline static size_t declaring_class_offset{};

    inline static uint32_t kAccFastNative = 0x00080000;
    inline static uint32_t kAccPreCompiled = 0x00200000;
    inline static uint32_t kAccCompileDontBother = 0x02000000;
    inline static uint32_t kAccDefaultConflict = 0x01000000;
    inline static uint32_t kAccFastInterpreterToInterpreterInvoke = 0x40000000;
    inline static uint32_t kAccIntrinsic = 0x80000000;
};

}  // namespace lsplant::art
