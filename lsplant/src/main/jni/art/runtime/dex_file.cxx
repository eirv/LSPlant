module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:dex_file;

import :common;
import :reflection;
import :art_method;
#endif

namespace lsplant::art {

class MemMap;
class OatDexFile;
enum class VerifyResult;

class DexFileContainer {};
class DexFileLoader {
    [[maybe_unused]] std::array<uint8_t, 512> reserved_{};
};

export class DexFile {
    struct Header {
        [[maybe_unused]] std::array<uint8_t, 8> magic_;
        uint32_t checksum_;  // See also location_checksum_
    };

    inline static auto DexFileLoader_constructor_ =
        ("_ZN3art13DexFileLoaderC2EPKhjRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE"_sym |
         "_ZN3art13DexFileLoaderC2EPKhmRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE"_sym)
            .as<void (DexFileLoader::*)(const uint8_t *base, size_t size,
                                        const std::string &location)>;

    inline static auto DexFileLoader_destructor_ =
        "_ZN3art13DexFileLoaderD2Ev"_sym.as<void (DexFileLoader::*)()>;

    inline static auto OpenOne_ =
        ("_ZN3art13DexFileLoader7OpenOneEjjPKNS_10OatDexFileEbbPNSt3__112basic_stringIcNS4_11char_traitsIcEENS4_9allocatorIcEEEE"_sym |
         "_ZN3art13DexFileLoader7OpenOneEmjPKNS_10OatDexFileEbbPNSt3__112basic_stringIcNS4_11char_traitsIcEENS4_9allocatorIcEEEE"_sym)
            .as<std::unique_ptr<const DexFile> (DexFileLoader::*)(
                size_t header_offset, uint32_t location_checksum, const OatDexFile *oat_dex_file,
                bool verify, bool verify_checksum, std::string *error_msg)>;

    inline static auto Open_ =
        "_ZN3art13DexFileLoader4OpenEjPKNS_10OatDexFileEbbPNSt3__112basic_stringIcNS4_11char_traitsIcEENS4_9allocatorIcEEEE"_sym
            .as<std::unique_ptr<const DexFile> (DexFileLoader::*)(
                uint32_t location_checksum, const OatDexFile *oat_dex_file, bool verify,
                bool verify_checksum, std::string *error_msg)>;

    inline static auto OpenCommon_ =
        ("_ZN3art13DexFileLoader10OpenCommonEPKhjS2_jRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPKNS_10OatDexFileEbbPS9_NS3_10unique_ptrINS_16DexFileContainerENS3_14default_deleteISH_EEEEPNS0_12VerifyResultE"_sym |
         "_ZN3art13DexFileLoader10OpenCommonEPKhmS2_mRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPKNS_10OatDexFileEbbPS9_NS3_10unique_ptrINS_16DexFileContainerENS3_14default_deleteISH_EEEEPNS0_12VerifyResultE"_sym)
            .as<std::unique_ptr<DexFile>(const uint8_t *base, size_t size, const uint8_t *data_base,
                                         size_t data_size, const std::string &location,
                                         uint32_t location_checksum, const OatDexFile *oat_dex_file,
                                         bool verify, bool verify_checksum, std::string *error_msg,
                                         std::unique_ptr<DexFileContainer> container,
                                         VerifyResult *verify_result)>;

    inline static auto OpenCommonWithoutContainer_ =
        ("_ZN3art7DexFile10OpenCommonEPKhjRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPKNS_10OatDexFileEbbPS9_PNS0_12VerifyResultE"_sym |
         "_ZN3art7DexFile10OpenCommonEPKhmRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPKNS_10OatDexFileEbbPS9_PNS0_12VerifyResultE"_sym)
            .as<std::unique_ptr<DexFile>(
                const uint8_t *base, size_t size, const std::string &location,
                uint32_t location_checksum, const OatDexFile *oat_dex_file, bool verify,
                bool verify_checksum, std::string *error_msg, VerifyResult *verify_result)>;

    inline static auto OpenMemory_ =
        ("_ZN3art7DexFile10OpenMemoryEPKhjRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPNS_6MemMapEPKNS_10OatDexFileEPS9_"_sym |
         "_ZN3art7DexFile10OpenMemoryEPKhmRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPNS_6MemMapEPKNS_10OatDexFileEPS9_"_sym)
            .as<std::unique_ptr<DexFile>(const uint8_t *dex_file, size_t size,
                                         const std::string &location, uint32_t location_checksum,
                                         MemMap *mem_map, const OatDexFile *oat_dex_file,
                                         std::string *error_msg)>;

    inline static auto OpenMemoryRaw_ =
        ("_ZN3art7DexFile10OpenMemoryEPKhjRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPNS_6MemMapEPKNS_7OatFileEPS9_"_sym |
         "_ZN3art7DexFile10OpenMemoryEPKhmRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPNS_6MemMapEPKNS_7OatFileEPS9_"_sym)
            .as<const DexFile *(const uint8_t *dex_file, size_t size, const std::string &location,
                                uint32_t location_checksum, MemMap *mem_map,
                                const OatDexFile *oat_dex_file, std::string *error_msg)>;

    inline static auto OpenMemoryWithoutOdex_ =
        ("_ZN3art7DexFile10OpenMemoryEPKhjRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPNS_6MemMapEPS9_"_sym |
         "_ZN3art7DexFile10OpenMemoryEPKhmRKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEEjPNS_6MemMapEPS9_"_sym)
            .as<const DexFile *(const uint8_t *dex_file, size_t size, const std::string &location,
                                uint32_t location_checksum, void *mem_map, std::string *error_msg)>;

    inline static auto DexFile_setTrusted_ =
        "_ZN3artL18DexFile_setTrustedEP7_JNIEnvP7_jclassP8_jobject"_sym
            .as<void(JNIEnv *env, jclass clazz, jobject j_cookie)>;

public:
    static bool IsMemoryDexSupported() { return cookie_field != nullptr; }

    static const DexFile *OpenMemory(const uint8_t *dex_file, size_t size, std::string location,
                                     std::string *error_msg) {
        auto checksum = reinterpret_cast<const Header *>(dex_file)->checksum_;
        if (OpenOne_ || Open_) [[likely]] {
            DexFileLoader loader{};
            DexFileLoader_constructor_(&loader, dex_file, size, location);
            const auto *dex =
                (OpenOne_
                     ? OpenOne_(&loader, 0, checksum, nullptr, kDebugBuild, kDebugBuild, error_msg)
                     : Open_(&loader, checksum, nullptr, kDebugBuild, kDebugBuild, error_msg))
                    .release();
            DexFileLoader_destructor_(&loader);
            return dex;
        }
        if (OpenCommon_) [[likely]] {
            return OpenCommon_(dex_file, size, nullptr, 0, location, checksum, nullptr, kDebugBuild,
                               kDebugBuild, error_msg, nullptr, nullptr)
                .release();
        }
        if (OpenCommonWithoutContainer_) [[likely]] {
            return OpenCommonWithoutContainer_(dex_file, size, location, checksum, nullptr,
                                               kDebugBuild, kDebugBuild, error_msg, nullptr)
                .release();
        }
        if (OpenMemory_) [[likely]] {
            return OpenMemory_(dex_file, size, location, checksum, nullptr, nullptr, error_msg)
                .release();
        }
        if (OpenMemoryRaw_) [[likely]] {
            return OpenMemoryRaw_(dex_file, size, location, checksum, nullptr, nullptr, error_msg);
        }
        if (OpenMemoryWithoutOdex_) [[likely]] {
            return OpenMemoryWithoutOdex_(dex_file, size, location, checksum, nullptr, error_msg);
        }
        if (error_msg) *error_msg = "null sym";
        return nullptr;
    }

    jobject ToJavaDexFile(JNIEnv *env, bool trusted = false) const {
        auto *java_dex_file = env->AllocObject(dex_file_class_ref.get(env).get());
        if (dex_file_start_index != std::numeric_limits<size_t>::max()) [[likely]] {
            auto cookie = JNI_NewLongArray(env, dex_file_start_index + 1);
            cookie[oat_file_index] = 0;
            cookie[dex_file_start_index] = reinterpret_cast<jlong>(this);
            cookie.commit();
            JNI_SetObjectField(env, java_dex_file, cookie_field, cookie);
            if (internal_cookie_field) [[likely]] {
                JNI_SetObjectField(env, java_dex_file, internal_cookie_field, cookie);
            }
            if (trusted) [[unlikely]] {
                SetTrusted(env, cookie.get());
            }
        } else {
            JNI_SetLongField(
                env, java_dex_file, cookie_field,
                static_cast<jlong>(reinterpret_cast<uintptr_t>(new std::vector{this})));
        }
        JNI_SetObjectField(env, java_dex_file, file_name_field, JNI_NewStringUTF(env, ""));
        if (guard_field) [[unlikely]] {
            auto guard = JNI_CallStaticObjectMethod(env, close_guard_class_ref.get(env),
                                                    get_close_guard_method);
            JNI_SetObjectField(env, java_dex_file, guard_field, guard);
        }
        return java_dex_file;
    }

    static bool SetTrusted(JNIEnv *env, jobject cookie) {
        if (DexFile_setTrusted_) {
            DexFile_setTrusted_(env, nullptr, cookie);
            return true;
        } else if (set_trusted_method) {
            JNI_CallStaticVoidMethod(env, dex_file_class_ref.get(env), set_trusted_method, cookie);
            return true;
        }
        return false;
    }

    static bool Init(JNIEnv *env, const HookHandler &handler) {
        auto sdk_int = GetAndroidApiLevel();

        auto dex_file_class = JNI_FindClass(env, "dalvik/system/DexFile");
        dex_file_class_ref.set(env, dex_file_class.get());

        if (sdk_int >= __ANDROID_API_P__) [[likely]] {
            if (!handler(DexFile_setTrusted_, true)) {
                set_trusted_method = JNI_GetStaticMethodID(env, dex_file_class, "setTrusted",
                                                           "(Ljava/lang/Object;)V");
                if (set_trusted_method) {
                    if (auto art_method = ArtMethod::FromJMethodID(env, dex_file_class.get(),
                                                                   set_trusted_method, true);
                        art_method && art_method->IsNative()) {
                        DexFile_setTrusted_ = art_method->GetData();
                    }
                } else {
                    LOGW("Failed to find DexFile.setTrusted; MakeDexFileTrusted will not work.");
                }
            }
        }

        if (sdk_int >= __ANDROID_API_U__) [[likely]] {
            if (!handler.dexfile(DexFileLoader_constructor_) ||
                !handler.dexfile(DexFileLoader_destructor_) || !handler.dexfile(OpenOne_, Open_)) {
                return true;
            }
        } else if (sdk_int >= __ANDROID_API_P__) {
            if (!handler.dexfile(OpenCommon_)) {
                return true;
            }
        } else if (sdk_int >= __ANDROID_API_O__) {
            if (!handler(OpenCommonWithoutContainer_)) {
                return true;
            }
        } else if (!handler(OpenMemory_, OpenMemoryRaw_, OpenMemoryWithoutOdex_)) [[unlikely]] {
            LOGE("Failed to find OpenMemory");
            return false;
        }

        if (sdk_int >= __ANDROID_API_M__) [[unlikely]] {
            cookie_field = JNI_GetFieldID(env, dex_file_class, "mCookie", "Ljava/lang/Object;");
        } else {
            cookie_field = JNI_GetFieldID(env, dex_file_class, "mCookie", "J");
            dex_file_start_index = std::numeric_limits<size_t>::max();
        }
        if (!cookie_field) [[unlikely]] {
            return false;
        }

        file_name_field = JNI_GetFieldID(env, dex_file_class, "mFileName", "Ljava/lang/String;");
        if (!file_name_field) [[unlikely]] {
            return false;
        }

        if (sdk_int >= __ANDROID_API_N__) [[likely]] {
            internal_cookie_field =
                JNI_GetFieldID(env, dex_file_class, "mInternalCookie", "Ljava/lang/Object;");
            if (!internal_cookie_field) [[unlikely]] {
                return false;
            }
            dex_file_start_index = 1U;
        }

        if (sdk_int <= __ANDROID_API_N__) [[unlikely]] {
            auto close_guard_class = JNI_FindClass(env, "dalvik/system/CloseGuard");
            if (!close_guard_class) [[unlikely]] {
                return true;
            }
            get_close_guard_method = JNI_GetStaticMethodID(env, close_guard_class, "get",
                                                           "()Ldalvik/system/CloseGuard;");
            if (!get_close_guard_method) [[unlikely]] {
                return true;
            }
            guard_field =
                JNI_GetFieldID(env, dex_file_class, "guard", "Ldalvik/system/CloseGuard;");
            if (guard_field) [[likely]] {
                close_guard_class_ref.set(env, close_guard_class.get());
            }
        }

        return true;
    }

private:
    inline static GlobalRef<jclass> dex_file_class_ref{};
    inline static GlobalRef<jclass> close_guard_class_ref{};
    inline static jfieldID cookie_field{};
    inline static jfieldID file_name_field{};
    inline static jfieldID internal_cookie_field{};
    inline static jfieldID guard_field{};
    inline static jmethodID set_trusted_method{};
    inline static jmethodID get_close_guard_method{};
    inline static size_t oat_file_index{};
    inline static size_t dex_file_start_index{};
};
}  // namespace lsplant::art
