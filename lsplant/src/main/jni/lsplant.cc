module;

#include "lsplant.hpp"

#include <android/api-level.h>
#include <fcntl.h>
#include <jni.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <bit>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
module lsplant;

import dex_builder;

import :common;
import :reflection;
import :arena_allocator;
import :clazz;
import :unsafe;
import :art_method;
import :class_linker;
import :dex_file;
import :instrumentation;
import :runtime;
import :stack;
import :thread;
import :thread_list;
import :scoped_gc_critical_section;
import :jit;
import :jit_code_cache;
import :jni_id_manager;
#endif

namespace lsplant {

using art::ArtMethod;
using art::ClassLinker;
using art::DexFile;
using art::Instrumentation;
using art::JavaDebuggableGuard;
using art::Runtime;
using art::StackVisitor;
using art::Thread;
using art::base::ArenaAllocator;
using art::base::MemMapArenaPool;
using art::gc::ScopedGCCriticalSection;
using art::jit::Jit;
using art::jit::JitCodeCache;
using art::jni::JniIdManager;
using art::mirror::Class;
using art::mirror::Unsafe;
using art::thread_list::ScopedSuspendAll;

using namespace std::string_view_literals;

namespace {

template <typename T, T... chars>
consteval auto operator""_uarr() {
    return std::array{static_cast<uint8_t>(chars)...};
}

auto [trampoline, entry_point_offset, art_method_offset] = [] consteval {
    if constexpr (is_arch_v<Arch::kArm64>) {
        return std::tuple{
            "\x60\x00\x00\x58\x10\x00\x40\xf8\x00\x02\x5f\xd6\xef\xcd\xab\x90\x78\x56\x34\x12"_uarr,
            // NOLINTNEXTLINE
            uint8_t{44u}, uintptr_t{12u}};
    } else if constexpr (is_arch_v<Arch::kArm>) {
        return std::tuple{"\x00\x00\x9f\xe5\x00\xf0\x90\xe5\x78\x56\x34\x12"_uarr,
                          // NOLINTNEXTLINE
                          uint8_t{32u}, uintptr_t{8u}};
    } else if constexpr (is_arch_v<Arch::kX86>) {
        return std::tuple{"\xb8\x78\x56\x34\x12\xff\x70\x00\xc3"_uarr,
                          // NOLINTNEXTLINE
                          uint8_t{56u}, uintptr_t{1u}};
    } else if constexpr (is_arch_v<Arch::kX64>) {
        return std::tuple{"\x48\xbf\xef\xcd\xab\x90\x78\x56\x34\x12\xff\x77\x00\xc3"_uarr,
                          // NOLINTNEXTLINE
                          uint8_t{96u}, uintptr_t{2u}};
    } else if constexpr (is_arch_v<Arch::kRiscv64>) {
        return std::tuple{
            "\x17\x05\x00\x00\x03\x35\xe5\x00\x03\x3e\x05\x00\x02\x8e\xef\xcd\xab\x90\x78\x56\x34\x12"_uarr,
            // NOLINTNEXTLINE
            uint8_t{84u}, uintptr_t{14u}};
    }
}();

JObjectMap hooker_class_loaders{};
uintptr_t loaded_classes{};
ArenaAllocator<MemMapArenaPool> arena_allocator{};

std::string generated_class_name;
std::string generated_source_name;
std::string generated_field_name;
std::string generated_method_name;

std::string generated_native_field_name;

bool InitConfig(const InitInfo &info) {
    if (info.generated_class_name.empty()) {
        LOGE("generated class name cannot be empty");
        return false;
    }
    if (info.generated_field_name.empty()) {
        LOGE("generated field name cannot be empty");
        return false;
    }
    if (info.generated_method_name.empty()) {
        LOGE("generated method name cannot be empty");
        return false;
    }
    generated_class_name = info.generated_class_name;
    generated_method_name = info.generated_method_name;
    generated_source_name = info.generated_source_name;
    if (generated_native_field_name.empty()) {
        generated_field_name = info.generated_field_name;
        generated_native_field_name = generated_field_name + "$native";
    } else if (generated_native_field_name != info.generated_field_name) {
        generated_field_name = info.generated_field_name;
    }
    return true;
}

template <size_t N>
inline void UpdateTrampoline(std::array<uint8_t, N> &data, uint8_t imm_offset, uint8_t imm) {
    data[imm_offset / CHAR_BIT] |= imm << (imm_offset % CHAR_BIT);
    data[(imm_offset / CHAR_BIT) + 1] |= imm >> (CHAR_BIT - imm_offset % CHAR_BIT);
}

bool InitNative(JNIEnv *env, const HookHandler &handler) {
    if (!Unsafe::Init(env)) [[unlikely]] {
        LOGE("Failed to init unsafe");
        return false;
    }
    if (!ArtMethod::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init art method");
        return false;
    }
    UpdateTrampoline(trampoline, entry_point_offset, ArtMethod::GetEntryPointOffset());
    if (!Thread::Init(handler)) [[unlikely]] {
        LOGE("Failed to init thread");
        return false;
    }
    if (!Instrumentation::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init instrumentation");
        return false;
    }
    if (!Class::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init mirror class");
        return false;
    }
    if (!Runtime::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init runtime");
        return false;
    }
    if (!ClassLinker::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init class linker");
        return false;
    }
    if (!ScopedSuspendAll::Init(handler)) [[unlikely]] {
        LOGE("Failed to init scoped suspend all");
        return false;
    }
    if (!ScopedGCCriticalSection::Init(handler)) [[unlikely]] {
        LOGE("Failed to init scoped gc critical section");
        return false;
    }
    if (!JitCodeCache::Init(handler)) [[unlikely]] {
        LOGE("Failed to init jit code cache");
        return false;
    }
    if (!Jit::Init(handler)) [[unlikely]] {
        LOGE("Failed to init jit");
        return false;
    }
    if (!DexFile::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init dex file");
        return false;
    }
    if (!JniIdManager::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init jni id manager");
        return false;
    }
    if (!StackVisitor::Init(env, handler)) [[unlikely]] {
        LOGE("Failed to init stack visitor");
        return false;
    }

    // This should always be the last one
    if (IsJavaDebuggable(env)) [[unlikely]] {
        // Make the runtime non-debuggable as a workaround
        // when ShouldUseInterpreterEntrypoint inlined
        Runtime::Current()->SetJavaDebuggable(Runtime::RuntimeDebugState::kNonJavaDebuggable);
    }
    return true;
}

auto OpenInMemoryHookerDex(JNIEnv *env, const slicer::MemView &image) {
    static constexpr uint32_t kMaxTryCount = 3;
    static uint32_t kDexLoadFailed = 0;

    if (kDexLoadFailed < kMaxTryCount && DexFile::IsMemoryDexSupported()) [[likely]] {
        auto *dex = arena_allocator.Alloc(image.size());
        std::memcpy(dex, image.ptr(), image.size());

        std::string err_msg;
        const auto *dex_file = DexFile::OpenMemory(
            reinterpret_cast<const uint8_t *>(dex), image.size(),
            generated_source_name.empty() ? "android" : generated_source_name, &err_msg);

        if (dex_file) [[likely]] {
            if constexpr (!kDebugBuild) {
                *reinterpret_cast<uint64_t *>(dex) = 0;
            }
            LOGV("Hooker dex loaded at %p", dex);
            kDexLoadFailed = 0;
            return ScopedLocalRef{env, dex_file->ToJavaDexFile(env)};
        }

        LOGE("Failed to open memory dex: %s", err_msg.data());
        if (GetAndroidApiLevel() >= __ANDROID_API_O__) [[likely]] {
            if (++kDexLoadFailed == kMaxTryCount) {
                LOGI("Open memory dex is unsupported; disable it.");
            }
        } else {
            return ScopedLocalRef<jobject>{env};
        }
    }

    if (auto dex_file_class = JNI_FindClass(env, "dalvik/system/DexFile"); dex_file_init_with_cl)
        [[likely]] {
        return JNI_NewObject(
            env, dex_file_class, dex_file_init_with_cl,
            JNI_NewObjectArray(
                env, 1, JNI_FindClass(env, "java/nio/ByteBuffer"),
                JNI_NewDirectByteBuffer(env, const_cast<void *>(image.ptr()), image.size())),
            nullptr, nullptr);
    } else if (dex_file_init) {
        return JNI_NewObject(
            env, dex_file_class, dex_file_init,
            JNI_NewDirectByteBuffer(env, const_cast<void *>(image.ptr()), image.size()));
    }

    return ScopedLocalRef<jobject>{env};
}

struct NativeHookerData {
    bool should_pack_receiver_and_args{};
    bool is_static{};
    mutable uint32_t references{};
    jfieldID data_field{};
    jmethodID callback_method{};
    NativeCallbackType native_entry{};
    ArtMethod *target{};
    const std::string shorty;
};

class ScopedNonNativeTargetMethod {
public:
    explicit ScopedNonNativeTargetMethod(const NativeHookerData *data) : data_{data} {
        if (data->references == 0 && !data->target->IsNative()) {
            data_ = nullptr;
        } else if (++(data->references) == 1) {
            data->target->SetNonNative();
        }
    }

    ~ScopedNonNativeTargetMethod() {
        if (data_ && --(data_->references) == 0) {
            data_->target->SetNative();
        }
    }

private:
    const NativeHookerData *data_;
};

void VNativeHookerEntry(JNIEnv *env, jclass hooker_class, jvalue &result, std::va_list va) {
    const NativeHookerData *data = nullptr;
    {
        auto *field = env->GetStaticFieldID(hooker_class, generated_native_field_name.c_str(),
                                            PickLP("J", "I"));
        if (!field) [[unlikely]] {
            return;
        }
        auto value = PickLP(env->functions->GetStaticLongField, env->functions->GetStaticIntField)(
            env, hooker_class, field);
        data = reinterpret_cast<decltype(data)>(static_cast<intptr_t>(value));
        if (!data) [[unlikely]] {
            return;
        }
    }

    ScopedNonNativeTargetMethod const unused{data};

    auto parameter_count = static_cast<jint>(data->shorty.size() - 1);
    jobject receiver = nullptr;
    auto *args = env->NewObjectArray(parameter_count + data->should_pack_receiver_and_args,
                                     object_class_ref.get(env).get(), nullptr);
    jint arg_index = 0;
    if (!data->is_static) {
        if (data->should_pack_receiver_and_args) {
            env->SetObjectArrayElement(args, 0, va_arg(va, jobject));
            ++arg_index;
        } else {
            receiver = va_arg(va, jobject);
        }
    }

    for (jint i = 0; parameter_count > i; ++i) {
        jobject wrapped = nullptr;
        switch (data->shorty[i + 1]) {
        case 'Z':
            wrapped = wrapper::Wrap(env, va_arg(va, jboolean));
            break;
        case 'B':
            wrapped = wrapper::Wrap(env, va_arg(va, jbyte));
            break;
        case 'S':
            wrapped = wrapper::Wrap(env, va_arg(va, jshort));
            break;
        case 'C':
            wrapped = wrapper::Wrap(env, va_arg(va, jchar));
            break;
        case 'I':
            wrapped = wrapper::Wrap(env, va_arg(va, jint));
            break;
        case 'F':
            wrapped = wrapper::Wrap(env, va_arg(va, jfloat));
            break;
        case 'J':
            wrapped = wrapper::Wrap(env, va_arg(va, jlong));
            break;
        case 'D':
            wrapped = wrapper::Wrap(env, va_arg(va, jdouble));
            break;
        case 'L':
            wrapped = va_arg(va, jobject);
            break;
        default:
            std::abort();
        }
        env->SetObjectArrayElement(args, i + arg_index, wrapped);
        env->DeleteLocalRef(wrapped);
    }

    jobject wrapped_result = nullptr;
    if (data->native_entry) [[unlikely]] {
        auto *arg = env->GetStaticObjectField(hooker_class, data->data_field);
        wrapped_result = data->native_entry(env, hooker_class, receiver, args, arg);
        env->DeleteLocalRef(arg);
    } else if (auto *callback = env->GetStaticObjectField(hooker_class, data->data_field))
        [[likely]] {
        if (data->should_pack_receiver_and_args) {
            wrapped_result = env->CallObjectMethod(callback, data->callback_method, args);
        } else {
            wrapped_result = env->CallObjectMethod(callback, data->callback_method, receiver, args);
        }
        env->DeleteLocalRef(callback);
    }

    if (receiver) env->DeleteLocalRef(receiver);
    env->DeleteLocalRef(args);

    switch (data->shorty[0]) {
    case 'Z':
        if (wrapped_result) [[likely]] {
            result.z = wrapper::UnwrapBoolean(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'B':
        if (wrapped_result) [[likely]] {
            result.b = wrapper::UnwrapByte(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'S':
        if (wrapped_result) [[likely]] {
            result.s = wrapper::UnwrapShort(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'C':
        if (wrapped_result) [[likely]] {
            result.c = wrapper::UnwrapChar(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'I':
        if (wrapped_result) [[likely]] {
            result.i = wrapper::UnwrapInt(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'F':
        if (wrapped_result) [[likely]] {
            result.f = wrapper::UnwrapFloat(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'J':
        if (wrapped_result) [[likely]] {
            result.j = wrapper::UnwrapLong(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'D':
        if (wrapped_result) [[likely]] {
            result.d = wrapper::UnwrapDouble(env, wrapped_result);
            env->DeleteLocalRef(wrapped_result);
        }
        break;
    case 'L':
        result.l = wrapped_result;
        break;
    case 'V':
        break;
    default:
        std::abort();
    }
}

template <typename T>
T NativeHookerEntry(JNIEnv *env, jclass hooker_class, ...) {
    jvalue result{};
    std::va_list va;
    va_start(va, hooker_class);
    VNativeHookerEntry(env, hooker_class, result, va);
    va_end(va);
    if constexpr (std::is_same_v<T, jboolean>) {
        return result.z;
    } else if constexpr (std::is_same_v<T, jbyte>) {
        return result.b;
    } else if constexpr (std::is_same_v<T, jshort>) {
        return result.s;
    } else if constexpr (std::is_same_v<T, jchar>) {
        return result.c;
    } else if constexpr (std::is_same_v<T, jint>) {
        return result.i;
    } else if constexpr (std::is_same_v<T, jfloat>) {
        return result.f;
    } else if constexpr (std::is_same_v<T, jlong>) {
        return result.j;
    } else if constexpr (std::is_same_v<T, jdouble>) {
        return result.d;
    } else if constexpr (std::is_same_v<T, void>) {
        return;
    } else {
        return result.l;
    }
}

std::tuple<jclass, jfieldID, ArtMethod *, jmethodID> BuildDex(
    JNIEnv *env, jobject class_loader, std::string_view shorty, std::string_view method_name,
    std::string_view hooker_class, std::string_view callback_name, bool is_static,
    bool is_fast_native, bool is_native_api, bool should_pack_receiver_and_args) {
    // NOLINTNEXTLINE
    using namespace startop::dex;

    if (shorty.empty()) [[unlikely]] {
        LOGE("Invalid shorty");
        return {};
    }

    DexBuilder dex_file;

    auto parameter_types = std::vector<TypeDescriptor>();
    parameter_types.reserve(shorty.size() - 1);
    auto return_type =
        shorty[0] == 'L' ? TypeDescriptor::Object : TypeDescriptor::FromDescriptor(shorty[0]);
    if (!is_static) parameter_types.push_back(TypeDescriptor::Object);  // this object
    for (const char &param : shorty.substr(1)) {
        parameter_types.push_back(param == 'L'
                                      ? TypeDescriptor::Object
                                      : TypeDescriptor::FromDescriptor(static_cast<char>(param)));
    }

    auto hooker_class_name = generated_class_name + std::to_string(loaded_classes++);

    ClassBuilder cbuilder{dex_file.MakeClass(hooker_class_name)};
    if (!generated_source_name.empty()) cbuilder.set_source_file(generated_source_name);

    auto hooker_type =
        is_native_api ? cbuilder.descriptor()
                      : TypeDescriptor::FromClassname({hooker_class.begin(), hooker_class.end()});

    auto *hooker_field =
        cbuilder
            .CreateField(generated_field_name, is_native_api ? TypeDescriptor::Object : hooker_type)
            .access_flags(dex::kAccPrivate | dex::kAccStatic)
            .Encode();

    auto hook_method_name = generated_method_name == "{target}"sv
                                ? std::string{method_name.begin(), method_name.end()}
                                : generated_method_name;
    auto hook_builder{
        cbuilder.CreateMethod(hook_method_name, Prototype{return_type, parameter_types})};
    if (is_fast_native) {
        hook_builder.access_flags(dex::kAccPublic | dex::kAccStatic | dex::kAccNative);
        cbuilder
            .CreateField(generated_native_field_name,
                         PickLP(TypeDescriptor::Long, TypeDescriptor::Int))
            .access_flags(dex::kAccPrivate | dex::kAccStatic)
            .Encode();
    } else {
        // allocate tmp first because of wide
        auto tmp{hook_builder.AllocRegister()};
        hook_builder.BuildConst(
            tmp, static_cast<int>(should_pack_receiver_and_args ? parameter_types.size()
                                                                : shorty.size() - 1));
        auto hook_params_array{hook_builder.AllocRegister()};
        hook_builder.BuildNewArray(hook_params_array, TypeDescriptor::Object, tmp);
        if (should_pack_receiver_and_args) {
            for (size_t i = 0U, j = 0U; i < parameter_types.size(); ++i, ++j) {
                hook_builder.BuildBoxIfPrimitive(Value::Parameter(j), parameter_types[i],
                                                 Value::Parameter(j));
                hook_builder.BuildConst(tmp, static_cast<int>(i));
                hook_builder.BuildAput(Instruction::Op::kAputObject, hook_params_array,
                                       Value::Parameter(j), tmp);
                if (parameter_types[i].is_wide()) ++j;
            }
        } else {
            for (size_t i = is_static ? 0U : 1U, j = is_static ? 0U : 1U;
                 i < parameter_types.size(); ++i, ++j) {
                hook_builder.BuildBoxIfPrimitive(Value::Parameter(j), parameter_types[i],
                                                 Value::Parameter(j));
                hook_builder.BuildConst(tmp, static_cast<int>(is_static ? i : (i - 1)));
                hook_builder.BuildAput(Instruction::Op::kAputObject, hook_params_array,
                                       Value::Parameter(j), tmp);
                if (parameter_types[i].is_wide()) ++j;
            }
        }
        auto handle_hook_method{dex_file.GetOrDeclareMethod(
            hooker_type, {callback_name.begin(), callback_name.end()},
            is_native_api
                ? Prototype{TypeDescriptor::Object, TypeDescriptor::Object,
                            TypeDescriptor::Object.ToArray(), TypeDescriptor::Object}
                : (should_pack_receiver_and_args
                       ? Prototype{TypeDescriptor::Object, TypeDescriptor::Object.ToArray()}
                       : Prototype{TypeDescriptor::Object, TypeDescriptor::Object,
                                   TypeDescriptor::Object.ToArray()}))};
        hook_builder.AddInstruction(
            Instruction::GetStaticObjectField(hooker_field->decl->orig_index, tmp));
        if (should_pack_receiver_and_args) {
            hook_builder.AddInstruction(Instruction::InvokeVirtualObject(handle_hook_method.id, tmp,
                                                                         tmp, hook_params_array));
        } else if (is_static) {
            if (shorty.size() == 1) {
                auto zero{hook_builder.AllocRegister()};
                hook_builder.BuildConst(zero, 0);
                if (is_native_api) {
                    hook_builder.AddInstruction(Instruction::InvokeStaticObject(
                        handle_hook_method.id, tmp, zero, hook_params_array, tmp));
                } else {
                    hook_builder.AddInstruction(Instruction::InvokeVirtualObject(
                        handle_hook_method.id, tmp, tmp, zero, hook_params_array));
                }
            } else {
                auto zero = Value::Parameter(0);
                hook_builder.BuildConst(zero, 0);
                if (is_native_api) {
                    hook_builder.AddInstruction(Instruction::InvokeStaticObject(
                        handle_hook_method.id, tmp, zero, hook_params_array, tmp));
                } else {
                    hook_builder.AddInstruction(Instruction::InvokeVirtualObject(
                        handle_hook_method.id, tmp, tmp, zero, hook_params_array));
                }
            }
        } else if (is_native_api) {
            hook_builder.AddInstruction(Instruction::InvokeStaticObject(
                handle_hook_method.id, tmp, Value::Parameter(0), hook_params_array, tmp));
        } else {
            hook_builder.AddInstruction(Instruction::InvokeVirtualObject(
                handle_hook_method.id, tmp, tmp, Value::Parameter(0), hook_params_array));
        }
        if (return_type == TypeDescriptor::Void) {
            hook_builder.BuildReturn();
        } else if (return_type.is_primitive()) {
            auto box_type{return_type.ToBoxType()};
            const ir::Type *type_def = dex_file.GetOrAddType(box_type);
            hook_builder.AddInstruction(Instruction::Cast(tmp, Value::Type(type_def->orig_index)));
            hook_builder.BuildUnBoxIfPrimitive(tmp, box_type, tmp);
            hook_builder.BuildReturn(tmp, false, return_type.is_wide());
        } else {
            // const ir::Type *type_def = dex_file.GetOrAddType(return_type);
            // hook_builder.AddInstruction(Instruction::Cast(tmp,
            // Value::Type(type_def->orig_index)));
            hook_builder.BuildReturn(tmp, true);
        }
    }
    auto *hook_method = hook_builder.Encode();

    auto backup_builder{cbuilder.CreateMethod(hook_method_name == "backup"sv ? "backup2" : "backup",
                                              Prototype{return_type, parameter_types})};
    if (return_type == TypeDescriptor::Void) {
        backup_builder.BuildReturn();
    } else if (return_type.is_wide()) {
        if (!should_pack_receiver_and_args && shorty.size() > 2) {
            auto zero = Value::Parameter(is_static ? 0 : 1);
            backup_builder.BuildConstWide(zero, 0);
            backup_builder.BuildReturn(zero, /*is_object=*/false, true);
        } else {
            LiveRegister const zero = backup_builder.AllocRegister();
            LiveRegister const zero_wide = backup_builder.AllocRegister();
            backup_builder.BuildConstWide(zero, 0);
            backup_builder.BuildReturn(zero, /*is_object=*/false, true);
        }
    } else {
        if (!should_pack_receiver_and_args && shorty.size() > 1) {
            auto zero = Value::Parameter(is_static ? 0 : 1);
            backup_builder.BuildConst(zero, 0);
            backup_builder.BuildReturn(zero, /*is_object=*/!return_type.is_primitive(), false);
        } else {
            LiveRegister const zero = backup_builder.AllocRegister();
            backup_builder.BuildConst(zero, 0);
            backup_builder.BuildReturn(zero, /*is_object=*/!return_type.is_primitive(), false);
        }
    }

    auto *backup_method = backup_builder.Encode();

    if (is_native_api && !is_fast_native) {
        auto native_builder{
            cbuilder
                .CreateMethod({callback_name.begin(), callback_name.end()},
                              Prototype{TypeDescriptor::Object, TypeDescriptor::Object,
                                        TypeDescriptor::Object.ToArray(), TypeDescriptor::Object})
                .access_flags(dex::kAccPrivate | dex::kAccStatic | dex::kAccNative)};
        native_builder.Encode();
    }

    slicer::MemView const image{dex_file.CreateImage()};

    auto java_dex_file = OpenInMemoryHookerDex(env, image);
    if (!java_dex_file) [[unlikely]] {
        return {};
    }

    auto hooker_class_loader = WrapScope(env, hooker_class_loaders.get(env, class_loader));
    if (!hooker_class_loader) {
        hooker_class_loader =
            JNI_NewObject(env, path_class_loader_ref.get(env), path_class_loader_init,
                          JNI_NewStringUTF(env, "/system"), class_loader);
        hooker_class_loaders.add(env, class_loader, hooker_class_loader.get());
    }
    auto *target_class =
        JNI_CallObjectMethod<jclass>(env, java_dex_file, load_class,
                                     JNI_NewStringUTF(env, hooker_class_name), hooker_class_loader)
            .release();
    if (!target_class) [[unlikely]] {
        return {};
    }

    if (is_fast_native) {
        void *native_hooker_entry = nullptr;
        switch (shorty[0]) {
        case 'Z':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jboolean>);
            break;
        case 'B':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jbyte>);
            break;
        case 'S':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jshort>);
            break;
        case 'I':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jint>);
            break;
        case 'F':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jfloat>);
            break;
        case 'J':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jlong>);
            break;
        case 'D':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jdouble>);
            break;
        case 'L':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<jobject>);
            break;
        case 'V':
            native_hooker_entry = reinterpret_cast<void *>(NativeHookerEntry<void>);
            break;
        default:
            std::abort();
        }
        auto jni_entry = std::array{
            JNINativeMethod{hook_method->decl->name->c_str(),
                            hook_method->decl->prototype->Signature().data(), native_hooker_entry}};
        JNI_RegisterNatives(env, target_class, jni_entry);
    }

    return {
        target_class,
        JNI_GetStaticFieldID(env, target_class, hooker_field->decl->name->c_str(),
                             hooker_field->decl->type->descriptor->c_str()),
        ArtMethod::FindStaticMethod(env, target_class, hook_method->decl->name->c_str(),
                                    hook_method->decl->prototype->Signature()),
        JNI_GetStaticMethodID(env, target_class, backup_method->decl->name->c_str(),
                              backup_method->decl->prototype->Signature()),
    };
}

static_assert(std::endian::native == std::endian::little, "Unsupported architecture");

union Trampoline {
public:
    uintptr_t address;
    unsigned count4k : 12;
    unsigned count16k : 14;
};

static_assert(sizeof(Trampoline) == sizeof(uintptr_t), "Unsupported architecture");
static_assert(std::atomic_uintptr_t::is_always_lock_free, "Unsupported architecture");

std::atomic_uintptr_t trampoline_pool{0};
std::atomic_flag trampoline_lock{false};
std::mutex trampoline_protect_lock;
constexpr size_t kTrampolineSize = RoundUpTo(trampoline.size(), sizeof(uint16_t));

ArtMethod **WriteTrampoline(char *address, ArtMethod *hook) {
    // Simply hide the address of the hook method
    if constexpr (is_arch_v<Arch::kArm64>) {
        if (auto hook_addr = reinterpret_cast<uint64_t>(hook); !(hook_addr >> 48)) [[likely]] {
            auto optimized_trampoline =
                "\x80\x46\xc2\xd2\x00\xcf\xaa\xf2\x60\x15\x92\xf2\x11\x00\x40\xf8\x20\x02\x5f\xd6"_uarr;
            static_assert(!is_arch_v<Arch::kArm64> ||
                          optimized_trampoline.size() <= kTrampolineSize);

            UpdateTrampoline(optimized_trampoline, 108, ArtMethod::GetEntryPointOffset());

            auto update_move_wide_imm = [&](size_t index, uint16_t imm) constexpr {
                struct MoveWideImm {
                    unsigned rd : 5;
                    unsigned imm : 16;
                    unsigned shift : 2;
                    unsigned op : 8;
                    unsigned size : 1;
                };
                static_assert(sizeof(MoveWideImm) == sizeof(uint32_t));
                auto &inst = reinterpret_cast<MoveWideImm *>(optimized_trampoline.data())[index];
                inst.imm = imm;
            };
            update_move_wide_imm(0, hook_addr >> 32);
            update_move_wide_imm(1, (hook_addr >> 16) & 0xFFFF);
            update_move_wide_imm(2, hook_addr & 0xFFFF);

            std::memcpy(address, optimized_trampoline.data(), optimized_trampoline.size());
            return reinterpret_cast<ArtMethod **>(address + 1);
        }
    }

    std::memcpy(address, trampoline.data(), trampoline.size());
    auto *entry_slot = reinterpret_cast<ArtMethod **>(address + art_method_offset);
    *entry_slot = hook;
    return entry_slot;
}

std::pair<void *, ArtMethod **> GenerateTrampolineFor(ArtMethod *hook) {
    static const size_t kPageSize = getpagesize();  // assume
    static const size_t kTrampolineNumPerPage = kPageSize / kTrampolineSize;

    uintptr_t address;
    for (unsigned count;;) {
        auto tl = Trampoline{.address = trampoline_pool.fetch_add(1, std::memory_order_release)};
        count = kPageSize == 16384 ? tl.count16k : tl.count4k;
        address = tl.address & ~(kPageSize - 1);
        if (address == 0 || count >= kTrampolineNumPerPage) {
            if (trampoline_lock.test_and_set(std::memory_order_acq_rel)) {
                trampoline_lock.wait(true, std::memory_order_acquire);
                continue;
            }
            auto library_fd = open(PickLP("/system/lib64/libstdc++.so", "/system/lib/libstdc++.so"),
                                   O_RDONLY | O_CLOEXEC, 0);
            if (library_fd > 0) [[likely]] {
                auto offset = lseek(library_fd, 0, SEEK_END);
                if (offset > 0) [[likely]] {
                    offset = __builtin_align_down(offset, kPageSize);
                } else {
                    offset = 0;
                }
                address = reinterpret_cast<uintptr_t>(mmap(nullptr, kPageSize,
                                                           PROT_READ | PROT_WRITE | PROT_EXEC,
                                                           MAP_PRIVATE, library_fd, offset));
                close(library_fd);
                if (address == reinterpret_cast<uintptr_t>(MAP_FAILED)) {
                    address = reinterpret_cast<uintptr_t>(mmap(nullptr, kPageSize,
                                                               PROT_READ | PROT_WRITE | PROT_EXEC,
                                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
                }
            } else {
                address = reinterpret_cast<uintptr_t>(mmap(nullptr, kPageSize,
                                                           PROT_READ | PROT_WRITE | PROT_EXEC,
                                                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            }
            if (address == reinterpret_cast<uintptr_t>(MAP_FAILED)) [[unlikely]] {
                PLOGE("mmap trampoline");
                trampoline_lock.clear(std::memory_order_release);
                trampoline_lock.notify_all();
                return {};
            }
            count = 0;
            tl.address = address;
            kPageSize == 16384 ? tl.count16k = count + 1 : tl.count4k = count + 1;
            trampoline_pool.store(tl.address, std::memory_order_release);
            trampoline_lock.clear(std::memory_order_release);
            trampoline_lock.notify_all();
        }
        LOGV("trampoline: count = %u, address = %zx, target = %zx", count, address,
             address + (static_cast<uintptr_t>(count) * kTrampolineSize));
        address = address + count * kTrampolineSize;
        break;
    }

    auto *address_ptr = reinterpret_cast<char *>(address);
    auto *address_ptr_start = __builtin_align_down(address_ptr, kPageSize);

    std::lock_guard const lk{trampoline_protect_lock};
    if (address & ~(kPageSize - 1) &&
        mprotect(address_ptr_start, kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        PLOGE("mprotect trampoline");
        return {};
    }

    auto *entry_slot = WriteTrampoline(address_ptr, hook);

    __builtin___clear_cache(address_ptr, reinterpret_cast<char *>(address + kTrampolineSize));

    mprotect(address_ptr_start, kPageSize, PROT_READ | PROT_EXEC);

    return {static_cast<void *>(address_ptr), entry_slot};
}

ArtMethod **DoHook(ArtMethod *target, ArtMethod *hook, ArtMethod *backup) {
    ScopedGCCriticalSection const section(Thread::Current(), art::gc::kGcCauseDebugger,
                                          art::gc::kCollectorTypeDebugger);
    ScopedSuspendAll const suspend("LSPlant Hook", false);
    LOGV("Hooking: target = %s(%p), hook = %s(%p), backup = %s(%p)", target->PrettyMethod().c_str(),
         target, hook->PrettyMethod(false).c_str(), hook, backup->PrettyMethod(false).c_str(),
         backup);

    if (auto [entrypoint, slot] = GenerateTrampolineFor(hook); !entrypoint) [[unlikely]] {
        LOGE("Failed to generate trampoline");
        return nullptr;
        // NOLINTNEXTLINE
    } else {
        target->SetNonIntrinsic();

        target->BackupTo(backup);

        target->SetNonCompilable();

        target->SetEntryPoint(entrypoint);

        backup->SetNonConstructor();

        LOGV("Done hook: target(%p:0x%x) -> %p; backup(%p:0x%x) -> %p; hook(%p:0x%x) -> %p", target,
             target->GetAccessFlags(), target->GetEntryPoint(), backup, backup->GetAccessFlags(),
             backup->GetEntryPoint(), hook, hook->GetAccessFlags(), hook->GetEntryPoint());

        return slot;
    }
}

bool DoUnHook(ArtMethod *target, ArtMethod *backup) {
    ScopedGCCriticalSection const section(Thread::Current(), art::gc::kGcCauseDebugger,
                                          art::gc::kCollectorTypeDebugger);
    ScopedSuspendAll const suspend("LSPlant Hook", false);
    LOGV("Unhooking: target = %p, backup = %p", target, backup);
    auto access_flags = target->GetAccessFlags();
    target->CopyFrom(backup);
    target->SetAccessFlags(access_flags);
    LOGV("Done unhook: target(%p:0x%x) -> %p; backup(%p:0x%x) -> %p;", target,
         target->GetAccessFlags(), target->GetEntryPoint(), backup, backup->GetAccessFlags(),
         backup->GetEntryPoint());
    return true;
}

bool IsProxyMethod(JNIEnv *env, ArtMethod *art_method, jobject reflected_method) {
    if (auto is_proxy_class = art_method->GetDeclaringClass()->IsProxyClass()) {
        return *is_proxy_class;
    }
    auto declaring_class =
        JNI_CallObjectMethod<jclass>(env, reflected_method, method_get_declaring_class);
    auto proxy_class = proxy_class_ref.get(env);
    if (declaring_class && proxy_class) [[likely]] {
        return env->IsAssignableFrom(proxy_class.get(), declaring_class.get());
    }
    return false;
}

template <typename Callback, bool kNativeApi = std::is_same_v<Callback, NativeCallbackType>,
          typename Result = std::conditional_t<kNativeApi, HookResult, jobject>>
    requires(kNativeApi || std::is_same_v<Callback, jobject>)
Result HookMethod(JNIEnv *env, jobject target_method, jobject hooker_object, Callback callback) {
    if (!target_method || !JNI_IsInstanceOf(env, target_method, executable_ref.get(env)))
        [[unlikely]] {
        LOGE("target method is not an executable");
        return {};
    }

    auto should_pack_receiver_and_args = false;

    if constexpr (kNativeApi) {
        if (!callback) [[unlikely]] {
            LOGE("callback is nullptr");
            return {};
        }
    } else {
        if (!JNI_IsInstanceOf(env, callback, executable_ref.get(env))) [[unlikely]] {
            LOGE("callback method is not an executable");
            return {};
        }
        auto shorty = __builtin_expect(ArtMethod::CanGetMethodShorty(), 1)
                          ? ArtMethod::FromReflectedMethod(env, callback)->GetShorty(env).data()
                          : GetReflectedMethodShorty(env, callback);
        if (shorty == "LL"sv) {
            should_pack_receiver_and_args = true;
        } else if (shorty != "LLL"sv) {
            LOGE("callback method is invalid");
            return {};
        }
    }

    ArtMethod *hook = nullptr;
    jmethodID backup_method = nullptr;
    jfieldID hooker_field = nullptr;

    auto *target = ArtMethod::FromReflectedMethod(env, target_method);
    auto is_proxy = IsProxyMethod(env, target, target_method);

    if (target->IsAbstract()) [[unlikely]] {
        LOGW("target method is not invokable");
        return {};
    }
    if (IsHooked(target) || IsBackup(target)) [[unlikely]] {
        LOGW("Skip duplicate hook");
        return {};
    }

    auto shorty = [=] -> std::string {
        if (ArtMethod::CanGetMethodShorty()) [[likely]] {
            if (is_proxy) [[unlikely]] {
                auto *np_method = target->GetInterfaceMethodIfProxy();
                if (np_method && np_method->IsAbstract()) [[likely]] {
                    auto sv = np_method->GetShorty(env);
                    return {sv.begin(), sv.end()};
                }
            } else {
                auto sv = target->GetShorty(env);
                return {sv.begin(), sv.end()};
            }
        }
        return GetReflectedMethodShorty(env, target_method);
    }();
    auto is_fast_native = target->IsFastNative();

    ScopedLocalRef<jclass> built_class{env};
    if constexpr (kNativeApi) {
        auto target_name = JNI_CallObjectMethod<jstring>(env, target_method, method_get_name);
        JUTFString const target_method_name(target_name);
        auto target_class =
            JNI_CallObjectMethod<jclass>(env, target_method, method_get_declaring_class);
        auto target_class_loader = JNI_CallObjectMethod(env, target_class, class_get_class_loader);
        const auto *native_method_name =
            target_method_name.get() == "native"sv ? "native2" : "native";
        std::tie(built_class, hooker_field, hook, backup_method) =
            WrapScope(env, BuildDex(env, target_class_loader.get(), shorty,
                                    target->IsConstructor() || target_method_name[0] == '<'
                                        ? "constructor"
                                        : target_method_name.get(),
                                    {}, native_method_name, target->IsStatic(), is_fast_native,
                                    kNativeApi, should_pack_receiver_and_args));
        if (built_class && !is_fast_native) [[likely]] {
            auto jni_entry = std::array{JNINativeMethod{
                native_method_name,
                "(Ljava/lang/Object;[Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                reinterpret_cast<void *>(callback)}};
            JNI_RegisterNatives(env, built_class, jni_entry);

            auto *native_method = ArtMethod::FindStaticMethod(
                env, built_class.get(), jni_entry[0].name, jni_entry[0].signature);
            if (generated_source_name.empty()) {
                StackVisitor::HideMethod(native_method);
            }
        }
    } else {
        auto callback_name = JNI_CallObjectMethod<jstring>(env, callback, method_get_name);
        JUTFString const callback_method_name(callback_name);
        auto target_name = JNI_CallObjectMethod<jstring>(env, target_method, method_get_name);
        JUTFString const target_method_name(target_name);
        auto callback_class =
            JNI_CallObjectMethod<jclass>(env, callback, method_get_declaring_class);
        auto callback_class_loader =
            JNI_CallObjectMethod(env, callback_class, class_get_class_loader);
        auto callback_class_name =
            JNI_CallObjectMethod<jstring>(env, callback_class, class_get_name);
        JUTFString const class_name(callback_class_name);
        if (!JNI_IsInstanceOf(env, hooker_object, callback_class)) [[unlikely]] {
            LOGE("callback_method is not a method of hooker_object");
            return {};
        }
        std::tie(built_class, hooker_field, hook, backup_method) = WrapScope(
            env, BuildDex(env, callback_class_loader.get(), shorty,
                          target->IsConstructor() || target_method_name[0] == '<'
                              ? "constructor"
                              : target_method_name.get(),
                          class_name.get(), callback_method_name.get(), target->IsStatic(),
                          is_fast_native, kNativeApi, should_pack_receiver_and_args));
    }
    if (!built_class || !hooker_field || !hook || !backup_method) [[unlikely]] {
        LOGE("Failed to generate hooker");
        return {};
    }

    if (is_fast_native) {
        auto *native_hooker_data = new NativeHookerData{
            .should_pack_receiver_and_args = should_pack_receiver_and_args,
            .is_static = target->IsStatic(),
            .data_field = hooker_field,
            .target = target,
            .shorty = shorty,
        };
        if constexpr (kNativeApi) {
            native_hooker_data->native_entry = callback;
        } else {
            native_hooker_data->callback_method = env->FromReflectedMethod(callback);
        }
        if (auto *native_hooker_data_field = JNI_GetStaticFieldID(
                env, built_class, generated_native_field_name, PickLP("J", "I"))) {
            JNI_SetStaticField(
                env, built_class, native_hooker_data_field,
                static_cast<jpointer>(reinterpret_cast<intptr_t>(native_hooker_data)));
        }
    }

    auto reflected_backup = JNI_ToReflectedMethod(env, built_class, backup_method, true);
    JNI_CallVoidMethod(env, reflected_backup, set_accessible, JNI_TRUE);
    if (GetAndroidApiLevel() >= __ANDROID_API_M__) {
        JNI_SetIntField(env, reflected_backup, method_access_flags_field,
                        JNI_GetIntField(env, target_method, method_access_flags_field));
        JNI_SetObjectField(env, reflected_backup, method_declaring_class_field,
                           JNI_GetObjectField(env, target_method, method_declaring_class_field));
    }

    auto *backup = ArtMethod::FromReflectedMethod(env, reflected_backup.get());

    JNI_SetStaticObjectField(env, built_class, hooker_field, hooker_object);

    if (auto *method_entry_slot = DoHook(target, hook, backup); method_entry_slot) [[likely]] {
        std::apply(
            [backup_method, target_method_id = env->FromReflectedMethod(target_method)](auto... v) {
                ((*v == target_method_id &&
                  (LOGD("Propagate internal used method because of hook"), *v = backup_method)) ||
                 ...);
            },
            kInternalMethods);
        RecordHooked(target, hook, backup, target->GetDeclaringClass()->GetClassDef(),
                     method_entry_slot, backup_method);
        if (!is_proxy) [[likely]] {
            RecordJitMovement(target, backup);
        } else {
            backuped_proxy_methods_.emplace(backup);
        }
        // Always record backup as deoptimized since we dont want its entrypoint to be updated
        // by FixupStaticTrampolines on hooker class
        // Used hook's declaring class here since backup's is no longer the same with hook's
        RecordDeoptimized(hook->GetDeclaringClass()->GetClassDef(), backup,
                          backup->GetEntryPoint());
        if (generated_source_name.empty()) {
            StackVisitor::HideMethod(hook);
        }
        if constexpr (kNativeApi) {
            return {built_class.release(), hooker_field, backup->ToJMethodID(),
                    reflected_backup.release()};
        } else {
            return reflected_backup.release();
        }
    }

    return {};
}

constexpr auto kNullPointerException = "java/lang/NullPointerException";
constexpr auto kIllegalArgumentException = "java/lang/IllegalArgumentException";

template <typename... Args>
void ThrowNewException(JNIEnv *env, const char *exception, const char *fmt, Args... args) {
    auto *exception_class = env->FindClass(exception);
    if (!exception_class) [[unlikely]] {
        return;
    }
    if constexpr (sizeof...(Args) != 0) {
        std::array<char, 1024> message{};
        snprintf(message.data(), message.size(), fmt, args...);
        env->ThrowNew(exception_class, message.data());
    } else {
        env->ThrowNew(exception_class, fmt);
    }
    env->DeleteLocalRef(exception_class);
}
}  // namespace

inline namespace v3 {
extern "C++" {

using ::lsplant::IsHooked;

[[maybe_unused, gnu::visibility("default")]]
bool Init(JNIEnv *env, const InitInfo &info) {
    static std::optional<bool> kInitialized{};

    TIMED_FUNCTION();

    if (kInitialized) [[unlikely]] {
        if (*kInitialized) return InitConfig(info);
        return false;
    }

    if (!info.inline_hooker || !info.art_symbol_resolver) [[unlikely]] {
        return false;
    }

    auto init_ok = InitConfig(info) && InitJNI(env) && InitNative(env, info);
    if (init_ok) {
        global_references.finalize(env);
    }
    kInitialized = init_ok;
    return init_ok;
}

[[maybe_unused, gnu::visibility("default")]]
jobject Hook(JNIEnv *env, jobject target_method, jobject hooker_object, jobject callback_method) {
    TIMED_FUNCTION();
    return HookMethod(env, target_method, hooker_object, callback_method);
}

[[maybe_unused, gnu::visibility("default")]]
HookResult HookUsingNativeAPI(JNIEnv *env, jobject target_method, NativeCallbackType callback,
                              jobject data) {
    TIMED_FUNCTION();
    return HookMethod(env, target_method, data, callback);
}

[[maybe_unused, gnu::visibility("default")]]
bool SetHookEnabled(JNIEnv *env, jobject target_method, bool enabled) {
    if (!target_method || !JNI_IsInstanceOf(env, target_method, executable_ref.get(env)))
        [[unlikely]] {
        LOGE("target method is not an executable");
        return false;
    }
    auto *target = ArtMethod::FromReflectedMethod(env, target_method);
    if (const auto &found = hooked_methods_.find(target); found != hooked_methods_.end())
        [[likely]] {
        std::lock_guard const lk{trampoline_protect_lock};
        auto &record = found->second;

        if (record.enabled == enabled) {
            return enabled;
        }

        auto page_size = getpagesize();
        auto addr = __builtin_align_down(record.method_entry_slot, page_size);
        mprotect(addr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);

        record.enabled = enabled;
        if (reinterpret_cast<uintptr_t>(record.method_entry_slot) & 1) [[likely]] {
            auto address = reinterpret_cast<char *>(record.method_entry_slot) - 1;
            auto entry_slot = WriteTrampoline(address, enabled ? record.backup : record.hook);
            if ((reinterpret_cast<uintptr_t>(entry_slot) & 1) == 0) [[unlikely]] {
                record.method_entry_slot = entry_slot;
            }
            __builtin___clear_cache(address, address + kTrampolineSize);
        } else {
            *record.method_entry_slot = enabled ? record.backup : record.hook;
        }

        mprotect(addr, page_size, PROT_READ | PROT_EXEC);

        return !enabled;
    }
    return false;
}

[[maybe_unused, gnu::visibility("default")]]
bool UnHook(JNIEnv *env, jobject target_method) {
    TIMED_FUNCTION();

    if (!target_method || !JNI_IsInstanceOf(env, target_method, executable_ref.get(env)))
        [[unlikely]] {
        LOGE("target method is not an executable");
        return false;
    }
    auto *target = ArtMethod::FromReflectedMethod(env, target_method);
    ArtMethod *backup = nullptr;
    jmethodID backup_method = nullptr;
    if (!hooked_methods_.erase_if(target, [&backup_method, &backup](const auto &it) {
            const auto &record = it.second;
            backup = record.backup;
            backup_method = record.backup_method;
            return true;
        })) [[unlikely]] {
        LOGE("Unable to unhook a method that is not hooked");
        return false;
    }
    // FIXME: not atomic, but should be fine
    backuped_methods_.erase(backup);
    backuped_proxy_methods_.erase(backup);
    hooked_classes_.erase_if(target->GetDeclaringClass()->GetClassDef(), [&target](auto &it) {
        it.second.erase(target);
        return it.second.empty();
    });
    if (DoUnHook(target, backup)) [[likely]] {
        std::apply(
            [backup_method, target_method_id = env->FromReflectedMethod(target_method)](auto... v) {
                ((*v == backup_method && (LOGD("Propagate internal used method because of unhook"),
                                          *v = target_method_id)) ||
                 ...);
            },
            kInternalMethods);
        return true;
    }
    return false;
}

[[maybe_unused, gnu::visibility("default")]]
bool IsHooked(JNIEnv *env, jobject method) {
    if (!method || !JNI_IsInstanceOf(env, method, executable_ref.get(env))) [[unlikely]] {
        LOGE("method is not an executable");
        return false;
    }
    auto *art_method = ArtMethod::FromReflectedMethod(env, method);
    return IsHooked(art_method);
}

[[maybe_unused, gnu::visibility("default")]]
bool Deoptimize(JNIEnv *env, jobject method) {
    if (!method || !JNI_IsInstanceOf(env, method, executable_ref.get(env))) [[unlikely]] {
        LOGE("method is not an executable");
        return false;
    }
    if (!ClassLinker::CanSetEntryPointsToInterpreter()) [[unlikely]] {
        LOGE("Deoptimize is not available");
        return false;
    }
    auto *art_method = ArtMethod::FromReflectedMethod(env, method);
    if (art_method->IsNative()) [[unlikely]] {
        LOGE("method is native");
        return false;
    }
    // record the original but not the backup
    RecordDeoptimized(art_method->GetDeclaringClass()->GetClassDef(), art_method,
                      art_method->GetEntryPoint());
    if (auto *backup = IsHooked(art_method); backup) {
        art_method = backup;
    }
    return ClassLinker::SetEntryPointsToInterpreter(art_method);
}

[[maybe_unused, gnu::visibility("default")]]
void *GetNativeFunction(JNIEnv *env, jobject method) {
    if (!method || !JNI_IsInstanceOf(env, method, executable_ref.get(env))) [[unlikely]] {
        LOGE("method is not an executable");
        return nullptr;
    }
    auto *art_method = ArtMethod::FromReflectedMethod(env, method);
    if (!art_method->IsNative()) [[unlikely]] {
        LOGE("method is not native");
        return nullptr;
    }
    return art_method->GetData();
}

[[maybe_unused, gnu::visibility("default")]]
bool MakeClassInheritable(JNIEnv *env, jclass target) {
    if (!target) [[unlikely]] {
        LOGE("target class is null");
        return false;
    }
    const auto constructors =
        JNI_CallObjectMethod<jobjectArray>(env, target, class_get_declared_constructors);
    auto access_flags = JNI_GetIntField<uint32_t>(env, target, class_access_flags);
    constexpr static uint32_t kAccFinal = 0x0010;
    JNI_SetIntField(env, target, class_access_flags, static_cast<jint>(access_flags & ~kAccFinal));
    for (const auto &constructor : constructors) {
        auto *method = ArtMethod::FromReflectedMethod(env, constructor.get());
        if (!method->IsPublic() && !method->IsProtected()) method->SetProtected();
        if (method->IsFinal()) method->SetNonFinal();
    }
    return true;
}

[[maybe_unused, gnu::visibility("default")]]
bool MakeDexFileTrusted(JNIEnv *env, jobject cookie) {
    if (!cookie) [[unlikely]] {
        return false;
    }
    JavaDebuggableGuard guard;
    return DexFile::SetTrusted(env, cookie);
}

[[maybe_unused, gnu::visibility("default")]]
std::expected<jobject, std::string> OpenInMemoryDexFile(JNIEnv *env, const void *dex, size_t size,
                                                        bool trusted) {
    if (!dex || size == 0) [[unlikely]] {
        LOGE("dex is empty");
        return nullptr;
    }
    if (!DexFile::IsMemoryDexSupported()) [[unlikely]] {
        LOGE("memory dex is not supported");
        return nullptr;
    }

    std::string err_msg;
    const auto *dex_file = DexFile::OpenMemory(
        reinterpret_cast<const uint8_t *>(dex), size,
        generated_source_name.empty() ? "android" : generated_source_name, &err_msg);

    if (dex_file) [[likely]] {
        return dex_file->ToJavaDexFile(env, trusted);
    }

    return std::unexpected{err_msg};
}

[[maybe_unused, gnu::visibility("default")]]
bool MakeMethodHidden(JNIEnv *env, jobject method) {
    if (!method || !JNI_IsInstanceOf(env, method, executable_ref.get(env))) [[unlikely]] {
        LOGE("method is not an executable");
        return false;
    }

    auto art_method = ArtMethod::FromReflectedMethod(env, method);
    return StackVisitor::HideMethod(art_method);
}

[[maybe_unused, gnu::visibility("default")]]
size_t MakeClassHidden(JNIEnv *env, jclass clazz) {
    if (!clazz) [[unlikely]] {
        LOGE("class is null");
        return 0;
    }

    ScopedGCCriticalSection section(Thread::Current(), art::gc::kGcCauseDebugger,
                                    art::gc::kCollectorTypeDebugger);
    ScopedSuspendAll suspend("LSPlant Hide", false);
    return StackVisitor::HideClass(env, clazz);
}

[[maybe_unused, gnu::visibility("default")]]
size_t MakeClassVisible(JNIEnv *env, jclass clazz) {
    if (!clazz) [[unlikely]] {
        LOGE("class is null");
        return 0;
    }

    ScopedGCCriticalSection section(Thread::Current(), art::gc::kGcCauseDebugger,
                                    art::gc::kCollectorTypeDebugger);
    ScopedSuspendAll suspend("LSPlant Hide", false);
    return StackVisitor::ShowClass(env, clazz);
}

[[maybe_unused, gnu::visibility("default")]]
std::expected<jobject, jthrowable> InvokeSpecial(JNIEnv *env, jobject executable,
                                                 jclass declaring_class, jobject receiver,
                                                 jobjectArray args_array) {
    if (!executable) [[unlikely]] {
        ThrowNewException(env, kNullPointerException, "null method");
        return nullptr;
    }

    auto method = ArtMethod::FromReflectedMethod(env, executable);
    if (!method) [[unlikely]] {
        ThrowNewException(env, kNullPointerException, "Executable.artMethod");
        return nullptr;
    }

    JNIScopeFrame frame{env, 16};

    auto *clazz = declaring_class;
    if (!clazz) {
        if (receiver && !method->IsStatic()) {
            clazz = env->GetObjectClass(receiver);
        } else {
            clazz = method->GetDeclaringClass()->ToReflectedClass(env);
        }
    }
    if (declaring_class && clazz != declaring_class &&
        !env->IsAssignableFrom(clazz, declaring_class)) [[unlikely]] {
        auto class_name_jstr = JNI_CallObjectMethod<jstring>(env, clazz, class_get_name);
        auto class_name = JUTFString{class_name_jstr};

        auto declaring_class_name_jstr =
            JNI_CallObjectMethod<jstring>(env, declaring_class, class_get_name);
        auto declaring_class_name = JUTFString{declaring_class_name_jstr};

        ThrowNewException(env, kIllegalArgumentException, "%s is not inherited from %s",
                          declaring_class_name.get(), class_name.get());
        return nullptr;
    }
    if (!receiver) {
        if (method->IsConstructor()) {
            receiver = env->AllocObject(declaring_class ?: clazz);
        } else if (!method->IsStatic()) [[unlikely]] {
            ThrowNewException(env, kNullPointerException, "null receiver");
            return nullptr;
        }
    }

    const char *shorty{};
    std::string shorty_str{};
    jint parameter_count{};

    if (ArtMethod::CanGetMethodShorty()) [[likely]] {
        ArtMethod *m = nullptr;
        if (IsProxyMethod(env, method, executable)) [[unlikely]] {
            auto np_method = method->GetInterfaceMethodIfProxy();
            if (np_method && np_method->IsAbstract()) [[likely]] {
                m = np_method;
            }
        } else {
            m = method;
        }
        if (m) [[likely]] {
            auto shorty_sv = m->GetShorty(env);
            shorty = shorty_sv.data();
            parameter_count = static_cast<jint>(shorty_sv.size());
        }
    }
    if (!shorty) [[unlikely]] {
        shorty_str = GetReflectedMethodShorty(env, executable);
        shorty = shorty_str.c_str();
        parameter_count = static_cast<jint>(shorty_str.size());
    } else {
        parameter_count = static_cast<jint>(std::strlen(shorty));
    }
    --parameter_count;

    if (auto args_count = args_array ? env->GetArrayLength(args_array) : 0;
        args_count != parameter_count) [[unlikely]] {
        ThrowNewException(env, kIllegalArgumentException,
                          "Wrong number of arguments; expected %d, got %d", parameter_count,
                          args_count);
        return nullptr;
    }

    jvalue args[parameter_count];
    std::memset(args, 0, parameter_count * sizeof(jvalue));

    for (jint i = 0; parameter_count > i; ++i) {
        auto *wrapped = env->GetObjectArrayElement(args_array, i);
        if (auto type = shorty[i + 1]; type != 'L') {
            lsplant::wrapper::Unwrap(env, type, args[i], wrapped);
            env->DeleteLocalRef(wrapped);
        } else {
            args[i].l = wrapped;
        }
    }

    jobject result = nullptr;

    if (auto mid = method->ToJMethodID(); method->IsStatic()) {
        switch (shorty[0]) {
        case 'Z': {
            auto v = env->CallStaticBooleanMethodA(clazz, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'B': {
            auto v = env->CallStaticByteMethodA(clazz, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'S': {
            auto v = env->CallStaticShortMethodA(clazz, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'C': {
            auto v = env->CallStaticCharMethodA(clazz, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'I': {
            auto v = env->CallStaticIntMethodA(clazz, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'F': {
            auto v = env->CallStaticFloatMethodA(clazz, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'J': {
            auto v = env->CallStaticLongMethodA(clazz, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'D': {
            auto v = env->CallStaticDoubleMethodA(clazz, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'L':
            result = env->CallStaticObjectMethodA(clazz, mid, args);
            if (!result && env->ExceptionCheck()) goto handle_exception;
            break;
        case 'V':
            env->CallStaticVoidMethodA(clazz, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            break;
        default:
            std::abort();
        }
    } else if (declaring_class) {
        switch (shorty[0]) {
        case 'Z': {
            auto v = env->CallNonvirtualBooleanMethodA(receiver, declaring_class, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'B': {
            auto v = env->CallNonvirtualByteMethodA(receiver, declaring_class, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'S': {
            auto v = env->CallNonvirtualShortMethodA(receiver, declaring_class, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'C': {
            auto v = env->CallNonvirtualCharMethodA(receiver, declaring_class, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'I': {
            auto v = env->CallNonvirtualIntMethodA(receiver, declaring_class, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'F': {
            auto v = env->CallNonvirtualFloatMethodA(receiver, declaring_class, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'J': {
            auto v = env->CallNonvirtualLongMethodA(receiver, declaring_class, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'D': {
            auto v = env->CallNonvirtualDoubleMethodA(receiver, declaring_class, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'L':
            result = env->CallNonvirtualObjectMethodA(receiver, declaring_class, mid, args);
            if (!result && env->ExceptionCheck()) goto handle_exception;
            break;
        case 'V':
            env->CallNonvirtualVoidMethodA(receiver, declaring_class, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            break;
        default:
            std::abort();
        }
    } else {
        switch (shorty[0]) {
        case 'Z': {
            auto v = env->CallBooleanMethodA(receiver, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'B': {
            auto v = env->CallByteMethodA(receiver, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'S': {
            auto v = env->CallShortMethodA(receiver, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'C': {
            auto v = env->CallCharMethodA(receiver, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'I': {
            auto v = env->CallIntMethodA(receiver, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'F': {
            auto v = env->CallFloatMethodA(receiver, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'J': {
            auto v = env->CallLongMethodA(receiver, mid, args);
            if (!v && env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'D': {
            auto v = env->CallDoubleMethodA(receiver, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            result = lsplant::wrapper::Wrap(env, v);
            break;
        }
        case 'L':
            result = env->CallObjectMethodA(receiver, mid, args);
            if (!result && env->ExceptionCheck()) goto handle_exception;
            break;
        case 'V':
            env->CallVoidMethodA(receiver, mid, args);
            if (env->ExceptionCheck()) goto handle_exception;
            break;
        default:
            std::abort();
        }
    }

    return frame.pop(result);

handle_exception:
    auto *exception = env->ExceptionOccurred();
    env->ExceptionClear();
    return std::unexpected{frame.pop(exception)};
}

}  // extern "C++"
}  // namespace v3

}  // namespace lsplant
