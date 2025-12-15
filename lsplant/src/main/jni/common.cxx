module;

#include <parallel_hashmap/phmap.h>
#include <sys/system_properties.h>

#include <list>
#include <shared_mutex>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:common;
export import hook_helper;
export import jni_helper;
#endif

export namespace lsplant {

namespace art {
class ArtMethod;
namespace mirror {
class Class;
}
namespace dex {
class ClassDef {};
}  // namespace dex

}  // namespace art

struct HookRecord {
    art::ArtMethod *hook;
    art::ArtMethod *backup;
    art::ArtMethod **method_entry_slot;
    jmethodID backup_method;
    bool enabled;
};

consteval auto IsDebugBuild() {
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

template <class K, class V, class Hash = phmap::priv::hash_default_hash<K>,
          class Eq = phmap::priv::hash_default_eq<K>,
          class Alloc = phmap::priv::Allocator<phmap::priv::Pair<const K, V>>, size_t N = 4>
using SharedHashMap = phmap::parallel_flat_hash_map<K, V, Hash, Eq, Alloc, N, std::shared_mutex>;

template <class T, class Hash = phmap::priv::hash_default_hash<T>,
          class Eq = phmap::priv::hash_default_eq<T>, class Alloc = phmap::priv::Allocator<T>,
          size_t N = 4>
using SharedHashSet = phmap::parallel_flat_hash_set<T, Hash, Eq, Alloc, N, std::shared_mutex>;

constexpr auto kDebugBuild = IsDebugBuild();
constexpr auto kPointerSize = sizeof(void *);

template <typename T>
constexpr inline auto RoundUpTo(T v, size_t size) {
    return __builtin_align_up(v, size);
}

constexpr inline auto &&PickLP(auto &&lp64, auto &&lp32) {
    if constexpr (is_arch_v<Arch::kLP64>) {
        return lp64;
    } else {
        return lp32;
    }
}

[[gnu::const]] inline auto GetAndroidApiLevel() {
    static auto kApiLevel = [] {
        std::array<char, PROP_VALUE_MAX> prop_value;
        __system_property_get("ro.build.version.sdk", prop_value.data());
        int base = atoi(prop_value.data());
        __system_property_get("ro.build.version.preview_sdk", prop_value.data());
        return base + atoi(prop_value.data());
    }();
    [[assume(kApiLevel >= __ANDROID_API__)]];
    return kApiLevel;
}

inline auto IsJavaDebuggable(JNIEnv *env) {
    static auto kDebuggable = [=] {
        auto sdk_int = GetAndroidApiLevel();
        if (sdk_int < __ANDROID_API_P__) [[unlikely]] {
            return false;
        }
        auto runtime_class = JNI_FindClass(env, "dalvik/system/VMRuntime");
        if (!runtime_class) [[unlikely]] {
            LOGE("Failed to find VMRuntime");
            return false;
        }
        auto get_runtime_method =
            JNI_GetStaticMethodID(env, runtime_class, "getRuntime", "()Ldalvik/system/VMRuntime;");
        if (!get_runtime_method) [[unlikely]] {
            LOGE("Failed to find VMRuntime.getRuntime()");
            return false;
        }
        auto is_debuggable_method = JNI_GetMethodID(env, runtime_class, "isJavaDebuggable", "()Z");
        if (!is_debuggable_method) [[unlikely]] {
            LOGE("Failed to find VMRuntime.isJavaDebuggable()");
            return false;
        }
        auto runtime = JNI_CallStaticObjectMethod(env, runtime_class, get_runtime_method);
        if (!runtime) [[unlikely]] {
            LOGE("Failed to get VMRuntime");
            return false;
        }
        bool is_debuggable =
            JNI_CallNonvirtualBooleanMethod(env, runtime, runtime_class, is_debuggable_method);
        LOGD("java runtime debuggable %s", is_debuggable ? "true" : "false");
        return is_debuggable;
    }();
    return kDebuggable;
}

inline SharedHashMap<art::ArtMethod *, HookRecord> hooked_methods_;

inline SharedHashMap<art::ArtMethod *, art::ArtMethod *> backuped_methods_;

inline SharedHashMap<const art::dex::ClassDef *, phmap::flat_hash_set<art::ArtMethod *>>
    hooked_classes_;

inline SharedHashMap<art::ArtMethod *, void *> deoptimized_methods_;

inline SharedHashMap<const art::dex::ClassDef *, phmap::flat_hash_set<art::ArtMethod *>>
    deoptimized_classes_;

inline SharedHashSet<art::ArtMethod *> backuped_proxy_methods_;

inline std::list<std::pair<art::ArtMethod *, art::ArtMethod *>> jit_movements_;
inline std::shared_mutex jit_movements_lock_;

inline art::ArtMethod *IsHooked(art::ArtMethod *art_method) {
    if (const auto &found = hooked_methods_.find(art_method); found != hooked_methods_.end()) {
        return found->second.backup;
    }
    return nullptr;
}

inline art::ArtMethod *IsBackup(art::ArtMethod *art_method) {
    if (const auto &found = backuped_methods_.find(art_method); found != backuped_methods_.end()) {
        return found->second;
    }
    return nullptr;
}

inline bool IsDeoptimized(art::ArtMethod *art_method) {
    return deoptimized_methods_.contains(art_method);
}

inline std::list<std::pair<art::ArtMethod *, art::ArtMethod *>> GetJitMovements() {
    std::unique_lock lk(jit_movements_lock_);
    return std::move(jit_movements_);
}

inline void RecordHooked(art::ArtMethod *target, art::ArtMethod *hook, art::ArtMethod *backup,
                         const art::dex::ClassDef *class_def, art::ArtMethod **method_entry_slot,
                         jmethodID backup_method) {
    hooked_classes_.lazy_emplace_l(
        class_def, [&target](auto &it) { it.second.emplace(target); },
        [&class_def, &target](const auto &ctor) {
            ctor(class_def, phmap::flat_hash_set<art::ArtMethod *>{target});
        });
    hooked_methods_.emplace(target, HookRecord{.hook = hook,
                                               .backup = backup,
                                               .method_entry_slot = method_entry_slot,
                                               .backup_method = backup_method,
                                               .enabled = true});
    backuped_methods_.emplace(backup, target);
}

inline void RecordDeoptimized(const art::dex::ClassDef *class_def, art::ArtMethod *art_method,
                              void *original_entry_point) {
    deoptimized_classes_[class_def].emplace(art_method);
    deoptimized_methods_.emplace(art_method, original_entry_point);
}

inline void RecordJitMovement(art::ArtMethod *target, art::ArtMethod *backup) {
    std::unique_lock lk(jit_movements_lock_);
    jit_movements_.emplace_back(target, backup);
}
}  // namespace lsplant
