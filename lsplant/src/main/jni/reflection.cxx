module;

#include <cstdlib>
#include <string>
#include <tuple>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:reflection;

import :common;
#endif

export namespace lsplant {

JObjectList global_references{};

template <typename T = jobject>
struct alignas(4) [[gnu::packed]] GlobalRef {
    jint index{-1};

    auto get(JNIEnv *env) const {
        auto obj = global_references.get(env, index);
        return WrapScope(env, reinterpret_cast<T>(obj));
    }

    void set(JNIEnv *env, T obj) {
        if (index < 0) {
            index = global_references.add(env, obj);
        } else {
            global_references.update(env, index, obj);
        }
    }
};

jmethodID method_get_name{};
jmethodID method_get_declaring_class{};
jfieldID method_access_flags_field{};
jfieldID method_declaring_class_field{};
jmethodID class_get_name{};
jmethodID class_get_class_loader{};
jmethodID class_get_declared_constructors{};
jmethodID class_get_declared_methods{};
jfieldID class_access_flags{};
jmethodID dex_file_init_with_cl{};
jmethodID dex_file_init{};
jmethodID load_class{};
jmethodID set_accessible{};
GlobalRef<jclass> executable_ref{-1};

// for proxy method
jmethodID method_get_parameter_types{};
jmethodID method_get_return_type{};

GlobalRef<jclass> path_class_loader_ref{};
jmethodID path_class_loader_init{};

jmethodID unsafe_get_int_method{};
jmethodID unsafe_put_int_method{};

GlobalRef<jclass> object_class_ref{};
GlobalRef<jclass> proxy_class_ref{};

GlobalRef<jclass> boolean_class_ref{};
GlobalRef<jclass> byte_class_ref{};
GlobalRef<jclass> short_class_ref{};
GlobalRef<jclass> char_class_ref{};
GlobalRef<jclass> int_class_ref{};
GlobalRef<jclass> float_class_ref{};
GlobalRef<jclass> long_class_ref{};
GlobalRef<jclass> double_class_ref{};

GlobalRef<jclass> primitive_boolean_class_ref{};
GlobalRef<jclass> primitive_byte_class_ref{};
GlobalRef<jclass> primitive_short_class_ref{};
GlobalRef<jclass> primitive_char_class_ref{};
GlobalRef<jclass> primitive_int_class_ref{};
GlobalRef<jclass> primitive_float_class_ref{};
GlobalRef<jclass> primitive_long_class_ref{};
GlobalRef<jclass> primitive_double_class_ref{};
GlobalRef<jclass> primitive_void_class_ref{};

jmethodID boolean_value_method{};
jmethodID byte_value_method{};
jmethodID short_value_method{};
jmethodID char_value_method{};
jmethodID int_value_method{};
jmethodID float_value_method{};
jmethodID long_value_method{};
jmethodID double_value_method{};

jmethodID boolean_value_of_method{};
jmethodID byte_value_of_method{};
jmethodID short_value_of_method{};
jmethodID char_value_of_method{};
jmethodID int_value_of_method{};
jmethodID float_value_of_method{};
jmethodID long_value_of_method{};
jmethodID double_value_of_method{};

constexpr auto kInternalMethods = std::make_tuple(
    &method_get_name, &method_get_declaring_class, &class_get_name, &class_get_class_loader,
    &class_get_declared_constructors, &class_get_declared_methods, &dex_file_init,
    &dex_file_init_with_cl, &load_class, &set_accessible, &method_get_parameter_types,
    &method_get_return_type, &path_class_loader_init, &unsafe_get_int_method,
    &unsafe_put_int_method);

bool InitJNI(JNIEnv *env) {
    int sdk_int = GetAndroidApiLevel();
    auto method = JNI_FindClass(env, "java/lang/reflect/Method");
    if (!method) [[unlikely]] {
        LOGE("Failed to find Method");
        return false;
    }
    auto executable = JNI_GetSuperclass(env, method);
    executable_ref.set(env, executable.get());

    if (method_get_name = JNI_GetMethodID(env, executable, "getName", "()Ljava/lang/String;");
        !method_get_name) [[unlikely]] {
        LOGE("Failed to find getName method");
        return false;
    }
    if (method_get_declaring_class =
            JNI_GetMethodID(env, executable, "getDeclaringClass", "()Ljava/lang/Class;");
        !method_get_declaring_class) [[unlikely]] {
        LOGE("Failed to find getDeclaringClass method");
        return false;
    }
    if (method_get_parameter_types =
            JNI_GetMethodID(env, executable, "getParameterTypes", "()[Ljava/lang/Class;");
        !method_get_parameter_types) [[unlikely]] {
        LOGE("Failed to find getParameterTypes method");
        return false;
    }
    if (method_get_return_type =
            JNI_GetMethodID(env, method, "getReturnType", "()Ljava/lang/Class;");
        !method_get_return_type) [[unlikely]] {
        LOGE("Failed to find getReturnType method");
        return false;
    }
    if (sdk_int >= __ANDROID_API_M__) [[likely]] {
        if (method_access_flags_field = JNI_GetFieldID(env, executable, "accessFlags", "I");
            !method_access_flags_field) [[unlikely]] {
            LOGE("Failed to find accessFlags field");
            return false;
        }
        if (method_declaring_class_field =
                JNI_GetFieldID(env, executable, "declaringClass", "Ljava/lang/Class;");
            !method_declaring_class_field) [[unlikely]] {
            LOGE("Failed to find declaringClass field");
            return false;
        }
    }

    auto clazz = JNI_FindClass(env, "java/lang/Class");
    if (!clazz) [[unlikely]] {
        LOGE("Failed to find Class");
        return false;
    }

    if (class_get_class_loader =
            JNI_GetMethodID(env, clazz, "getClassLoader", "()Ljava/lang/ClassLoader;");
        !class_get_class_loader) [[unlikely]] {
        LOGE("Failed to find getClassLoader");
        return false;
    }

    if (class_get_declared_constructors = JNI_GetMethodID(env, clazz, "getDeclaredConstructors",
                                                          "()[Ljava/lang/reflect/Constructor;");
        !class_get_declared_constructors) [[unlikely]] {
        LOGE("Failed to find getDeclaredConstructors");
        return false;
    }

    if (class_get_declared_methods =
            JNI_GetMethodID(env, clazz, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
        !class_get_declared_methods) [[unlikely]] {
        LOGE("Failed to find getDeclaredMethods");
        return false;
    }

    if (class_get_name = JNI_GetMethodID(env, clazz, "getName", "()Ljava/lang/String;");
        !class_get_name) [[unlikely]] {
        LOGE("Failed to find getName");
        return false;
    }

    if (class_access_flags = JNI_GetFieldID(env, clazz, "accessFlags", "I"); !class_access_flags)
        [[unlikely]] {
        LOGE("Failed to find Class.accessFlags");
        return false;
    }
    auto path_class_loader = JNI_FindClass(env, "dalvik/system/PathClassLoader");
    if (!path_class_loader) [[unlikely]] {
        LOGE("Failed to find PathClassLoader");
        return false;
    } else {
        path_class_loader_ref.set(env, path_class_loader.get());
    }
    if (path_class_loader_init = JNI_GetMethodID(env, path_class_loader, "<init>",
                                                 "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
        !path_class_loader_init) [[unlikely]] {
        LOGE("Failed to find PathClassLoader.<init>");
        return false;
    }
    auto dex_file_class = JNI_FindClass(env, "dalvik/system/DexFile");
    if (!dex_file_class) [[unlikely]] {
        LOGE("Failed to find DexFile");
        return false;
    }
    if (sdk_int >= __ANDROID_API_Q__) [[likely]] {
        dex_file_init_with_cl = JNI_GetMethodID(
            env, dex_file_class, "<init>",
            "([Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;[Ldalvik/system/DexPathList$Element;)V");
    } else if (sdk_int >= __ANDROID_API_O__) {
        dex_file_init = JNI_GetMethodID(env, dex_file_class, "<init>", "(Ljava/nio/ByteBuffer;)V");
    }
    if (sdk_int >= __ANDROID_API_O__ && !dex_file_init_with_cl && !dex_file_init) [[unlikely]] {
        LOGE("Failed to find DexFile.<init>");
        return false;
    }
    if (load_class =
            JNI_GetMethodID(env, dex_file_class, "loadClass",
                            "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/Class;");
        !load_class) [[unlikely]] {
        LOGE("Failed to find a suitable way to load class");
        return false;
    }
    auto accessible_object = JNI_FindClass(env, "java/lang/reflect/AccessibleObject");
    if (!accessible_object) [[unlikely]] {
        LOGE("Failed to find AccessibleObject");
        return false;
    }
    if (set_accessible = JNI_GetMethodID(env, accessible_object, "setAccessible", "(Z)V");
        !set_accessible) [[unlikely]] {
        LOGE("Failed to find AccessibleObject.setAccessible");
        return false;
    }

    object_class_ref.set(env, JNI_FindClass(env, "java/lang/Object").get());
    proxy_class_ref.set(env, JNI_FindClass(env, "java/lang/reflect/Proxy").get());

    auto number_class = JNI_FindClass(env, "java/lang/Number");
    auto get_primitive_class = [=]<ScopeOrClass Class>(Class &&wrapper_class) {
        auto field = JNI_GetStaticFieldID(env, wrapper_class, "TYPE", "Ljava/lang/Class;");
        return JNI_GetStaticObjectField<jclass>(env, wrapper_class, field);
    };

    {
        auto boolean_class = JNI_FindClass(env, "java/lang/Boolean");
        boolean_class_ref.set(env, boolean_class.get());
        primitive_boolean_class_ref.set(env, get_primitive_class(boolean_class).get());
        boolean_value_method = JNI_GetMethodID(env, boolean_class, "booleanValue", "()Z");
        boolean_value_of_method =
            JNI_GetStaticMethodID(env, boolean_class, "valueOf", "(Z)Ljava/lang/Boolean;");
    }
    {
        auto byte_class = JNI_FindClass(env, "java/lang/Byte");
        byte_class_ref.set(env, byte_class.get());
        primitive_byte_class_ref.set(env, get_primitive_class(byte_class).get());
        byte_value_method = JNI_GetMethodID(env, number_class, "byteValue", "()B");
        byte_value_of_method =
            JNI_GetStaticMethodID(env, byte_class, "valueOf", "(B)Ljava/lang/Byte;");
    }
    {
        auto short_class = JNI_FindClass(env, "java/lang/Short");
        short_class_ref.set(env, short_class.get());
        primitive_short_class_ref.set(env, get_primitive_class(short_class).get());
        short_value_method = JNI_GetMethodID(env, number_class, "shortValue", "()S");
        short_value_of_method =
            JNI_GetStaticMethodID(env, short_class, "valueOf", "(S)Ljava/lang/Short;");
    }
    {
        auto char_class = JNI_FindClass(env, "java/lang/Character");
        char_class_ref.set(env, char_class.get());
        primitive_char_class_ref.set(env, get_primitive_class(char_class).get());
        char_value_method = JNI_GetMethodID(env, char_class, "charValue", "()C");
        char_value_of_method =
            JNI_GetStaticMethodID(env, char_class, "valueOf", "(C)Ljava/lang/Character;");
    }
    {
        auto int_class = JNI_FindClass(env, "java/lang/Integer");
        int_class_ref.set(env, int_class.get());
        primitive_int_class_ref.set(env, get_primitive_class(int_class).get());
        int_value_method = JNI_GetMethodID(env, number_class, "intValue", "()I");
        int_value_of_method =
            JNI_GetStaticMethodID(env, int_class, "valueOf", "(I)Ljava/lang/Integer;");
    }
    {
        auto float_class = JNI_FindClass(env, "java/lang/Float");
        float_class_ref.set(env, float_class.get());
        primitive_float_class_ref.set(env, get_primitive_class(float_class).get());
        float_value_method = JNI_GetMethodID(env, number_class, "floatValue", "()F");
        float_value_of_method =
            JNI_GetStaticMethodID(env, float_class, "valueOf", "(F)Ljava/lang/Float;");
    }
    {
        auto long_class = JNI_FindClass(env, "java/lang/Long");
        long_class_ref.set(env, long_class.get());
        primitive_long_class_ref.set(env, get_primitive_class(long_class).get());
        long_value_method = JNI_GetMethodID(env, number_class, "longValue", "()J");
        long_value_of_method =
            JNI_GetStaticMethodID(env, long_class, "valueOf", "(J)Ljava/lang/Long;");
    }
    {
        auto double_class = JNI_FindClass(env, "java/lang/Double");
        double_class_ref.set(env, double_class.get());
        primitive_double_class_ref.set(env, get_primitive_class(double_class).get());
        double_value_method = JNI_GetMethodID(env, number_class, "doubleValue", "()D");
        double_value_of_method =
            JNI_GetStaticMethodID(env, double_class, "valueOf", "(D)Ljava/lang/Double;");
    }
    {
        auto void_class = JNI_FindClass(env, "java/lang/Void");
        primitive_void_class_ref.set(env, get_primitive_class(void_class).get());
    }

    return true;
}

std::string GetReflectedMethodShorty(JNIEnv *env, jobject reflected_method) {
    const auto return_type = JNI_CallObjectMethod(env, reflected_method, method_get_return_type);
    const auto parameter_types =
        JNI_CallObjectMethod<jobjectArray>(env, reflected_method, method_get_parameter_types);

    auto int_type = primitive_int_class_ref.get(env);
    auto long_type = primitive_long_class_ref.get(env);
    auto float_type = primitive_float_class_ref.get(env);
    auto double_type = primitive_double_class_ref.get(env);
    auto boolean_type = primitive_boolean_class_ref.get(env);
    auto byte_type = primitive_byte_class_ref.get(env);
    auto char_type = primitive_char_class_ref.get(env);
    auto short_type = primitive_short_class_ref.get(env);
    auto void_type = primitive_void_class_ref.get(env);

    std::string out;
    out.reserve(parameter_types.size() + 1);
    auto type_to_shorty = [&]<ScopeOrObject Object>(Object &&type) {
        if (JNI_IsSameObject(env, type, int_type)) return 'I';
        if (JNI_IsSameObject(env, type, long_type)) return 'J';
        if (JNI_IsSameObject(env, type, float_type)) return 'F';
        if (JNI_IsSameObject(env, type, double_type)) return 'D';
        if (JNI_IsSameObject(env, type, boolean_type)) return 'Z';
        if (JNI_IsSameObject(env, type, byte_type)) return 'B';
        if (JNI_IsSameObject(env, type, char_type)) return 'C';
        if (JNI_IsSameObject(env, type, short_type)) return 'S';
        if (JNI_IsSameObject(env, type, void_type)) return 'V';
        return 'L';
    };
    out += type_to_shorty(return_type);
    for (const auto &param : parameter_types) {
        out += type_to_shorty(param);
    }
    return out;
}

namespace wrapper {

jobject Wrap(JNIEnv *env, jboolean value) {
    jvalue arg{.z = value};
    auto wrapper_class = boolean_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), boolean_value_of_method, &arg);
    return wrapped;
}
jobject Wrap(JNIEnv *env, jbyte value) {
    jvalue arg{.b = value};
    auto wrapper_class = byte_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), byte_value_of_method, &arg);
    return wrapped;
}
jobject Wrap(JNIEnv *env, jshort value) {
    jvalue arg{.s = value};
    auto wrapper_class = short_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), short_value_of_method, &arg);
    return wrapped;
}
jobject Wrap(JNIEnv *env, jchar value) {
    jvalue arg{.c = value};
    auto wrapper_class = char_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), char_value_of_method, &arg);
    return wrapped;
}
jobject Wrap(JNIEnv *env, jint value) {
    jvalue arg{.i = value};
    auto wrapper_class = int_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), int_value_of_method, &arg);
    return wrapped;
}
jobject Wrap(JNIEnv *env, jfloat value) {
    jvalue arg{.f = value};
    auto wrapper_class = float_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), float_value_of_method, &arg);
    return wrapped;
}
jobject Wrap(JNIEnv *env, jlong value) {
    jvalue arg{.j = value};
    auto wrapper_class = long_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), long_value_of_method, &arg);
    return wrapped;
}
jobject Wrap(JNIEnv *env, jdouble value) {
    jvalue arg{.d = value};
    auto wrapper_class = double_class_ref.get(env);
    auto wrapped = env->CallStaticObjectMethodA(wrapper_class.get(), double_value_of_method, &arg);
    return wrapped;
}

jboolean UnwrapBoolean(JNIEnv *env, jobject wrapped) {
    return env->CallBooleanMethod(wrapped, boolean_value_method);
}
jbyte UnwrapByte(JNIEnv *env, jobject wrapped) {
    return env->CallByteMethod(wrapped, byte_value_method);
}
jshort UnwrapShort(JNIEnv *env, jobject wrapped) {
    return env->CallShortMethod(wrapped, short_value_method);
}
jchar UnwrapChar(JNIEnv *env, jobject wrapped) {
    return env->CallCharMethod(wrapped, char_value_method);
}
jint UnwrapInt(JNIEnv *env, jobject wrapped) {
    return env->CallIntMethod(wrapped, int_value_method);
}
jfloat UnwrapFloat(JNIEnv *env, jobject wrapped) {
    return env->CallFloatMethod(wrapped, float_value_method);
}
jlong UnwrapLong(JNIEnv *env, jobject wrapped) {
    return env->CallLongMethod(wrapped, long_value_method);
}
jdouble UnwrapDouble(JNIEnv *env, jobject wrapped) {
    return env->CallDoubleMethod(wrapped, double_value_method);
}

jobject Wrap(JNIEnv *env, char type, jvalue &value) {
    switch (type) {
    case 'Z':
        return Wrap(env, value.z);
    case 'B':
        return Wrap(env, value.b);
    case 'S':
        return Wrap(env, value.s);
    case 'C':
        return Wrap(env, value.c);
    case 'I':
        return Wrap(env, value.i);
    case 'F':
        return Wrap(env, value.f);
    case 'J':
        return Wrap(env, value.j);
    case 'D':
        return Wrap(env, value.d);
    case 'V':
        return nullptr;
    default:
        std::abort();
    }
}

void Unwrap(JNIEnv *env, char type, jvalue &value, jobject wrapped) {
    switch (type) {
    case 'Z':
        value.z = UnwrapBoolean(env, wrapped);
        break;
    case 'B':
        value.b = UnwrapByte(env, wrapped);
        break;
    case 'S':
        value.s = UnwrapShort(env, wrapped);
        break;
    case 'C':
        value.c = UnwrapChar(env, wrapped);
        break;
    case 'I':
        value.i = UnwrapInt(env, wrapped);
        break;
    case 'F':
        value.f = UnwrapFloat(env, wrapped);
        break;
    case 'J':
        value.j = UnwrapLong(env, wrapped);
        break;
    case 'D':
        value.d = UnwrapDouble(env, wrapped);
        break;
    case 'L':
        value.l = wrapped;
        break;
    case 'V':
        value.l = nullptr;
        break;
    default:
        std::abort();
    }
}

}  // namespace wrapper

}  // namespace lsplant
