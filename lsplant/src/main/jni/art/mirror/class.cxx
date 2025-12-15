module;

#include <parallel_hashmap/phmap.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:clazz;

import :common;
import :reflection;
import :array_slice;
import :unsafe;
import :art_method;
import :handle;
import :instrumentation;
import :thread;
#endif

export namespace lsplant::art::mirror {

class Class {
    inline static auto GetClassDef_ =
        "_ZN3art6mirror5Class11GetClassDefEv"_sym.as<const dex::ClassDef *(Class::*)()>;

    using BackupMethods = phmap::flat_hash_map<art::ArtMethod *, void *>;
    inline static phmap::flat_hash_map<const art::Thread *,
                                       phmap::flat_hash_map<const dex::ClassDef *, BackupMethods>>
        backup_methods_;
    inline static std::mutex backup_methods_lock_;

    inline static uint8_t initialized_status{};

    static void BackupClassMethods(const dex::ClassDef *class_def, art::Thread *self) {
        BackupMethods out;
        if (!class_def) return;
        hooked_classes_.if_contains(class_def, [&out](const auto &it) {
            for (auto method : it.second) {
                if (method->IsStatic()) {
                    LOGV("Backup hooked method %p because of initialization", method);
                    out.emplace(method, method->GetEntryPoint());
                }
            }
        });
        deoptimized_classes_.if_contains(class_def, [&out](const auto &it) {
            for (auto method : it.second) {
                if (method->IsStatic()) {
                    LOGV("Backup deoptimized method %p because of initialization", method);
                    out.emplace(method, method->GetEntryPoint());
                }
            }
        });
        if (!out.empty()) [[unlikely]] {
            std::unique_lock const lk(backup_methods_lock_);
            backup_methods_[self].emplace(class_def, std::move(out));
        }
    }

    inline static auto SetClassStatus_ =
        "_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS_11ClassStatusEPNS_6ThreadE"_sym.hook->*
        []<Backup auto backup>(TrivialHandle<Class> h, uint8_t new_status,
                               Thread *self) static -> void {
        if (new_status == initialized_status) {
            BackupClassMethods(GetClassDef_(h.Get()), self);
        }
        return backup(h, new_status, self);
    };

    inline static auto SetStatus_ =
        "_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS1_6StatusEPNS_6ThreadE"_sym.hook->*
        []<Backup auto backup>(Handle<Class> h, int new_status, Thread *self) static -> void {
        if (new_status == static_cast<int>(initialized_status)) {
            BackupClassMethods(GetClassDef_(h.Get()), self);
        }
        return backup(h, new_status, self);
    };

    inline static auto TrivialSetStatus_ =
        "_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS1_6StatusEPNS_6ThreadE"_sym.hook->*
        []<Backup auto backup>(TrivialHandle<Class> h, uint32_t new_status,
                               Thread *self) static -> void {
        if (new_status == initialized_status) {
            BackupClassMethods(GetClassDef_(h.Get()), self);
        }
        return backup(h, new_status, self);
    };

    inline static auto ClassSetStatus_ =
        "_ZN3art6mirror5Class9SetStatusENS1_6StatusEPNS_6ThreadE"_sym.hook->*
        []<MemBackup auto backup>(Class *thiz, int new_status, Thread *self) static -> void {
        if (new_status == static_cast<int>(initialized_status)) {
            BackupClassMethods(GetClassDef_(thiz), self);
        }
        return backup(thiz, new_status, self);
    };

    [[nodiscard]] base::ArraySlice<ArtMethod> GetDeclaredMethods() const {
        auto methods = static_cast<uintptr_t>(*reinterpret_cast<jlong *>(
            reinterpret_cast<uintptr_t>(this) + methods_or_direct_methods_offset));
        auto array = reinterpret_cast<ArtMethod *>(methods + sizeof(size_t));
        auto length = *reinterpret_cast<size_t *>(methods);
        auto element_size = ArtMethod::GetArtMethodSize();
        return {array, length, element_size};
    }

    bool VisitMethodsForMOrLower(const std::function<bool(ArtMethod *)> &visitor,
                                 size_t offset) const {
        if (GetAndroidApiLevel() == __ANDROID_API_M__) {
            auto methods = static_cast<uintptr_t>(
                *reinterpret_cast<jlong *>(reinterpret_cast<uintptr_t>(this) + offset));
            auto length = *reinterpret_cast<size_t *>(methods);
            auto element_size = ArtMethod::GetArtMethodSize();
            for (size_t i = 0; length > i; ++i) {
                auto method = (i * element_size) + methods + sizeof(size_t);
                if (visitor(reinterpret_cast<ArtMethod *>(method))) return true;
            }
        } else {
            auto base_offset = Unsafe::GetObjectArrayBaseOffset();
            auto methods = static_cast<uintptr_t>(
                *reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(this) + offset));
            auto length = *reinterpret_cast<jint *>(methods + base_offset - sizeof(jint));
            for (size_t i = 0; length > i; ++i) {
                auto method = *reinterpret_cast<uint32_t *>((i * sizeof(uint32_t)) + base_offset);
                if (visitor(reinterpret_cast<ArtMethod *>(static_cast<uintptr_t>(method)))) {
                    return true;
                }
            }
        }
        return false;
    }

public:
    const dex::ClassDef *GetClassDef() { return GetClassDef_(this); }

    std::optional<uint32_t> GetAccessFlags() {
        if (!access_flags_offset) [[unlikely]] {
            return {};
        }
        return *reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(this) +
                                             access_flags_offset);
    }

    std::optional<bool> IsProxyClass() {
        auto access_flags = GetAccessFlags();
        if (!access_flags) [[unlikely]] {
            return {};
        }
        return (*access_flags & kAccClassIsProxy) != 0;
    }

    bool VisitMethods(const std::function<bool(ArtMethod *)> &visitor) const {
        if (GetAndroidApiLevel() >= __ANDROID_API_N__) [[likely]] {
            if (!methods_or_direct_methods_offset) [[unlikely]] {
                return false;
            }
            for (auto &method : GetDeclaredMethods()) {
                if (visitor(&method)) return true;
            }
            return true;
        } else if (methods_or_direct_methods_offset && virtual_methods_offset) [[likely]] {
            if (!VisitMethodsForMOrLower(visitor, methods_or_direct_methods_offset)) {
                VisitMethodsForMOrLower(visitor, virtual_methods_offset);
            }
            return true;
        }
        return false;
    }

    jclass ToReflectedClass(JNIEnv *env) {
        return reinterpret_cast<jclass>(Thread::Current()->EncodeJObject(env, this));
    }

    static void VisitMethods(JNIEnv *env, jclass clazz,
                             const std::function<bool(ArtMethod *)> &visitor) {
        auto mirror_class = FromReflectedClass(env, clazz);
        if (mirror_class && mirror_class->VisitMethods(visitor)) [[likely]] {
            return;
        }
        auto visit = [&, env, clazz](jmethodID getter) {
            auto reflected_methods = JNI_CallObjectMethod<jobjectArray>(env, clazz, getter);
            for (const auto &reflected_method : reflected_methods) {
                auto method = ArtMethod::FromReflectedMethod(env, reflected_method.get());
                if (visitor(method)) return true;
            }
            return false;
        };
        visit(class_get_declared_constructors) || visit(class_get_declared_methods);
    }

    static Class *FromReflectedClass(JNIEnv *env, jclass clazz) {
        return static_cast<Class *>(Thread::Current()->DecodeJObject(env, clazz));
    }

    static auto PopBackup(const dex::ClassDef *class_def, art::Thread *self) {
        BackupMethods methods;
        if (!backup_methods_.size()) [[likely]] {
            return methods;
        }
        if (class_def) {
            std::unique_lock lk(backup_methods_lock_);
            for (auto it = backup_methods_.begin(); it != backup_methods_.end();) {
                if (auto found = it->second.find(class_def); found != it->second.end()) {
                    methods.merge(std::move(found->second));
                    it->second.erase(found);
                }
                if (it->second.empty()) {
                    backup_methods_.erase(it++);
                } else {
                    ++it;
                }
            }
        } else if (self) {
            std::unique_lock lk(backup_methods_lock_);
            if (auto found = backup_methods_.find(self); found != backup_methods_.end()) {
                for (auto it = found->second.begin(); it != found->second.end();) {
                    methods.merge(std::move(it->second));
                    found->second.erase(it++);
                }
                backup_methods_.erase(found);
            }
        }
        return methods;
    }

    static bool Init(JNIEnv *env, const HookHandler &handler) {
        if (!handler(GetClassDef_)) [[unlikely]] {
            LOGE("Failed to find GetClassDef");
            return false;
        }

        int sdk_int = GetAndroidApiLevel();

        if (sdk_int < __ANDROID_API_O__) [[unlikely]] {
            if (!handler(SetStatus_, ClassSetStatus_)) {
                LOGE("Failed to hook SetStatus");
                return false;
            }
        } else {
            if (!handler(SetClassStatus_, TrivialSetStatus_)) {
                LOGE("Failed to hook SetStatus");
                return false;
            }
        }

        if (sdk_int >= __ANDROID_API_R__) [[likely]] {
            initialized_status = 15;
        } else if (sdk_int >= __ANDROID_API_P__) {
            initialized_status = 14;
        } else if (sdk_int == __ANDROID_API_O_MR1__) {
            initialized_status = 11;
        } else {
            initialized_status = 10;
        }

        if (auto unsafe = Unsafe::GetUnsafe(env); unsafe && Unsafe::HasObjectFieldOffset())
            [[likely]] {
            auto class_class = JNI_FindClass(env, "java/lang/Class");
            auto get_offset_from_class = [&, env](const char *name, const char *sig) {
                auto field = JNI_GetFieldID(env, class_class, name, sig);
                if (!field) [[unlikely]] {
                    LOGW("Failed to find Class.%s", name);
                    return 0U;
                }
                return unsafe.ObjectFieldOffset(
                    JNI_ToReflectedField(env, class_class, field).get());
            };

            access_flags_offset = get_offset_from_class("accessFlags", "I");
            if (sdk_int >= __ANDROID_API_N__) [[likely]] {
                methods_or_direct_methods_offset = get_offset_from_class("methods", "J");
            } else if (Unsafe::GetObjectArrayBaseOffset() == 0) {
                LOGW(
                    "Methods cannot be iterated over; the base address of Object[].class is unknown.");
            } else if (sdk_int == __ANDROID_API_M__) {
                methods_or_direct_methods_offset = get_offset_from_class("directMethods", "J");
                virtual_methods_offset = get_offset_from_class("virtualMethods", "J");
            } else {
                methods_or_direct_methods_offset =
                    get_offset_from_class("directMethods", "[Ljava/lang/reflect/ArtMethod;");
                virtual_methods_offset =
                    get_offset_from_class("virtualMethods", "[Ljava/lang/reflect/ArtMethod;");
            }
        }

        return true;
    }

private:
    static constexpr uint32_t kAccClassIsProxy = 0x00040000;

    inline static uint32_t access_flags_offset{};
    inline static uint32_t methods_or_direct_methods_offset{};
    inline static uint32_t virtual_methods_offset{};
};

}  // namespace lsplant::art::mirror
