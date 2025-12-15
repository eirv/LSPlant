#define LOG_TAG "LSPlant-test"

#include <dlfcn.h>
#include <jni.h>
#include <syscall.h>
#include <unistd.h>

#include <csignal>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "art/runtime/art_method.hpp"
#include "c_string.hpp"
#include "dobby_compat.h"
#include "logging.hpp"
#include "lsplant.hpp"
#include "utils/jni_helper.hpp"
#include "xdl_ext.h"

namespace {

using namespace lsplant;

bool init_result{};

jclass wrapper_class{};
jmethodID wrapper_value_of_method{};
jmethodID wrapper_get_value_method{};

jobject NewPointerWrapper(JNIEnv *env, void *ptr) {
    auto value = static_cast<jlong>(reinterpret_cast<intptr_t>(ptr));
    return env->CallStaticObjectMethod(wrapper_class, wrapper_value_of_method, value);
}

void *GetPointerFromWrapper(JNIEnv *env, jobject wrapper) {
    auto value = env->CallNonvirtualLongMethod(wrapper, wrapper_class, wrapper_get_value_method);
    return reinterpret_cast<void *>(static_cast<intptr_t>(value));
}

jobject NativeCallback(JNIEnv *env, jclass /*hooker_class*/, jobject this_object,
                       jobjectArray args_array, jobject data) {
    auto *data_array = reinterpret_cast<jobjectArray>(data);
    auto *receiver = env->GetObjectArrayElement(data_array, 0);
    auto *callback_wrapper = env->GetObjectArrayElement(data_array, 1);
    auto *callback = static_cast<jmethodID>(GetPointerFromWrapper(env, callback_wrapper));
    env->DeleteLocalRef(callback_wrapper);
    auto *result = env->CallObjectMethod(receiver, callback, this_object, args_array);
    env->DeleteLocalRef(receiver);
    return result;
}

std::string GetFilePath(std::string_view pathname, const char *name) {
    auto pos = pathname.rfind('/');
    if (pos == std::string_view::npos) {
        return name;
    }
    return std::string{pathname.begin(), pathname.begin() + pos + 1} + name;
}
}  // namespace

extern "C" {
JNIEXPORT jboolean Java_org_lsposed_lsplant_LSPTest_initHooker(JNIEnv *env, jclass /*type*/) {
    return init_result;
}

JNIEXPORT jobject Java_org_lsposed_lsplant_Hooker_doHook(JNIEnv *env, jclass /*type*/,
                                                         jobject target, jobject hooker,
                                                         jobject callback) {
    return Hook(env, target, hooker, callback);
}

JNIEXPORT jobject Java_org_lsposed_lsplant_Hooker_doHookUsingNativeAPI(JNIEnv *env, jclass /*type*/,
                                                                       jobject target,
                                                                       jobject hooker,
                                                                       jobject callback) {
    auto object_class = env->FindClass("java/lang/Object");
    auto object_array = env->NewObjectArray(2, object_class, nullptr);
    env->DeleteLocalRef(object_class);
    env->SetObjectArrayElement(object_array, 0, hooker);
    auto callback_method = env->FromReflectedMethod(callback);
    auto callback_method_wrapper = NewPointerWrapper(env, callback_method);
    env->SetObjectArrayElement(object_array, 1, callback_method_wrapper);
    env->DeleteLocalRef(callback_method_wrapper);
    auto result = HookUsingNativeAPI(env, target, NativeCallback, object_array);
    env->DeleteLocalRef(object_array);
    return result.backup_method_object;
}

JNIEXPORT jboolean Java_org_lsposed_lsplant_Hooker_doUnhook(JNIEnv *env, jclass /*type*/,
                                                            jobject target) {
    return UnHook(env, target);
}

JNIEXPORT jboolean Java_org_lsposed_lsplant_Hooker_deoptimize(JNIEnv *env, jclass /*type*/,
                                                              jobject target) {
    return Deoptimize(env, target);
}

JNIEXPORT jobject Java_org_lsposed_lsplant_Hooker_findMethod(JNIEnv *env, jclass /*type*/,
                                                             jclass clazz, jstring name_jstr,
                                                             jstring signature_jstr,
                                                             jboolean is_static) {
    if (!clazz || !name_jstr || !signature_jstr) return nullptr;
    
    JUTFString const name{env, name_jstr};
    JUTFString const signature{env, signature_jstr};
    
    auto method_id = is_static ? env->GetStaticMethodID(clazz, name, signature)
                               : env->GetMethodID(clazz, name, signature);
    if (!method_id) return nullptr;
    
    auto art_method = art::ArtMethod::FromJMethodID(env, clazz, method_id, is_static);
    
    auto is_constructor = art_method->IsConstructor();
    if (is_constructor) art_method->SetNonConstructor();
    auto reflected_method = env->ToReflectedMethod(clazz, method_id, is_static);
    if (is_constructor) art_method->SetConstructor();
    
    return reflected_method;
}

JNIEXPORT jobject Java_org_lsposed_lsplant_Hooker_invokeSpecial(JNIEnv *env, jclass /*type*/,
                                                                jobject executable,
                                                                jclass declaring_class,
                                                                jobject receiver,
                                                                jobjectArray args) {
    if (auto result = InvokeSpecial(env, executable, declaring_class, receiver, args)) {
        return *result;
    } else {
        env->Throw(result.error());
        return nullptr;
    }
}

JNIEXPORT void Java_org_lsposed_lsplant_LSPTest_killAfterDelay(JNIEnv * /*env*/, jclass /*type*/,
                                                               jlong milliseconds) {
    std::thread([=] {
        usleep(milliseconds * 1000);

        auto pid = static_cast<pid_t>(syscall(__NR_getpid));

        syscall(__NR_kill, pid, SIGKILL);
        usleep(100'000);

        syscall(__NR_exit_group, 1);
        _exit(1);
    }).detach();
}

jint JNI_OnLoad(JavaVM *vm, void * /*reserved*/) {
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    xdl_info_t info{};
    void *cache = nullptr;

    auto *art =
        xdl_addr4(reinterpret_cast<void *>(env->functions->GetVersion), &info, &cache, XDL_NON_SYM)
            ? cache
            : xdl_open("libart.so", XDL_DEFAULT);
    void *dexfile = nullptr;

    if (!art) {
        LOGW("libart.so not found");
        return JNI_ERR;
    }

    std::unordered_map<void *, std::string_view> symbol_mappings{};

    InitInfo const init_info{
        .inline_hooker = [&](auto target, auto hooker, auto backup) -> void * {
            if (auto it = symbol_mappings.find(target); it != symbol_mappings.end()) {
                LOGI("Hooking %s@%p -> %p", CString{it->second}.get(), target, hooker);
            } else {
                LOGI("Hooking %p -> %p", target, hooker);
            }

            if (DobbyHook(target, hooker, backup) == 0) {
                return target;
            }
            return nullptr;
        },
        .inline_unhooker = [](auto handle) -> bool { return DobbyDestroy(handle) == 0; },
        .art_symbol_resolver = [=, &symbol_mappings](auto symbol, auto) -> void * {
            CString const str{symbol};
            auto *addr = xdl_sym(art, str, nullptr);
            if (!addr) {
                addr = xdl_dsym(art, str, nullptr);
            }
            if (!addr) {
                LOGW("Symbol '%s' not found in libart.so", str.get());
                return nullptr;
            }
            symbol_mappings[addr] = symbol;
            return addr;
        },
        .art_symbol_prefix_resolver = [=, &symbol_mappings](auto prefix) -> void * {
            CString const str{prefix};
            auto *addr = xdl_dsym_prefix(art, str, nullptr);
            if (!addr) {
                addr = xdl_sym(art, str, nullptr);
            }
            if (!addr) {
                LOGW("Symbol '%s'* not found in libart.so", str.get());
                return nullptr;
            }
            symbol_mappings[addr] = prefix;
            return addr;
        },
        .dexfile_symbol_resolver = [&, art](auto symbol, auto) -> void * {
            if (!dexfile) {
                if (cache && info.dli_fname) {
                    auto pathname = GetFilePath(info.dli_fname, "libdexfile.so");
                    dexfile = xdl_open(pathname.c_str(), XDL_DEFAULT);
                }
                if (!dexfile) dexfile = xdl_open("libdexfile.so", XDL_DEFAULT);
                if (!dexfile) {
                    LOGW("libdexfile.so not found");
                    dexfile = art;
                }
            }
            CString const str{symbol};
            auto *addr = xdl_sym(dexfile, str, nullptr);
            if (!addr) {
                addr = xdl_dsym(dexfile, str, nullptr);
            }
            if (!addr) {
                LOGW("Symbol '%s' not found in libdexfile.so", str.get());
                return nullptr;
            }
            symbol_mappings[addr] = symbol;
            return addr;
        },
    };

    init_result = Init(env, init_info);

    if (cache) {
        xdl_addr_clean(&cache);
    } else {
        xdl_close(art);
    }
    if (dexfile && dexfile != art) {
        xdl_close(dexfile);
    }

    wrapper_class = reinterpret_cast<jclass>(env->NewGlobalRef(env->FindClass("java/lang/Long")));
    wrapper_value_of_method =
        env->GetStaticMethodID(wrapper_class, "valueOf", "(J)Ljava/lang/Long;");
    wrapper_get_value_method = env->GetMethodID(wrapper_class, "longValue", "()J");

    return JNI_VERSION_1_6;
}
}
