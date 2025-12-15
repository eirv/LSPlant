# LSPlant

![](https://img.shields.io/badge/license-LGPL--3.0-orange.svg)
![](https://img.shields.io/badge/Android-5.0%20--%2016-blue.svg)
![](https://img.shields.io/badge/arch-armeabi--v7a%20%7C%20arm64--v8a%20%7C%20x86%20%7C%20x86--64%20%7C%20riscv64-brightgreen.svg)
![](https://github.com/eirv/LSPlant/actions/workflows/build.yml/badge.svg?branch=master&event=push)
![](https://img.shields.io/maven-central/v/org.lsposed.lsplant/lsplant.svg)

[**English**](README.md)
 | **中文**

> [!Tip]
> 由于我对这个库的修改比较激进，可能无法在部分设备上达到预期的运行效果。  
> 但不妨大胆一试！

> LSPlant 是作用于 Android ART 的 hook 库，提供 Java 方法的 hook/unhook 和内联去优化功能。

该项目是 LSPosed 框架的一部分，遵循 GNU 宽通用公共许可证。

## 特性

+ 支持 Android 5.0 - 16（API 级别 21 - 36）
+ 支持 armeabi-v7a、arm64-v8a、x86、x86-64、riscv64
+ 支持自定义的内联 hook 框架和 ART 符号解析器

## (重要）与上游库行为不一致的地方

+ 导出函数的命名空间由 `lsplant::v2` 变为 `lsplant::v3`。
+ 默认仅编译动态库，如果需要编译静态库，可以把 CMake 选项 `LSPLANT_BUILD_STATIC` 设置为 `ON`。
+ `InitInfo::inline_hooker` 会传入三个参数，返回当前 hook 操作的句柄。
+ `InitInfo::inline_unhooker` 可为空，传入的参数为先前 hook 操作返回的句柄。
+ `InitInfo::art_symbol_resolver` 会传入两个参数，第二个参数为当前符号的 GNU 哈希。
+ `InitInfo::art_symbol_prefix_resolver` 可为空。
+ `InitInfo::dexfile_symbol_resolver` 可为空，用于查找 `libdexfile.so` 中的函数地址，通过这个在内存中加载并隐藏 DEX，仅会在安卓 9 及以上被使用。
+ `InitInfo::generated_class_name` 变为已生成类名的前缀；默认值由 `"LSPHooker_"` 变为 `"LSPHooker$"`。
+ `InitInfo::generated_source_name` 可为空，如果为空，则会让已生成的存根类在调用栈中隐藏。
+ `InitInfo::generated_field_name` 默认值由 `"hooker"` 变为 `"data"`。
+ `InitInfo::generated_*` 可以通过重新调用 `Init()` 函数来更新这些值。
+ `Hook()` 返回的 `jobject` 不再是全局引用，而是变为了局部引用；回调方法支持两种签名，详情见[Hook](#2-hook)。

## 文档

https://lsposed.org/LSPlant/namespacelsplant.html

## 快速开始

```gradle
repositories {
    mavenCentral()
}

android {
    buildFeatures {
        prefab true
    }
}

dependencies {
    implementation "org.lsposed.lsplant:lsplant:+"
}
```

如果你不想在 APK 中包含 `libc++_shared.so`，可以使用 `lsplant-standalone` 代替：

```gradle
dependencies {
    implementation "org.lsposed.lsplant:lsplant-standalone:+"
}
```

### 1. 在 JNI_OnLoad 中初始化 LSPlant

初始化 LSPlant，以便进行后续的 hook。它主要预取所需的符号并 hook 一些函数。

+ `env` 是 JNI 环境指针。

+ `info` 是初始化信息。

  基本上，info 提供了内联 hook 函数和解除 hook 函数，以及 `libart.so` 的符号解析器，用于 hook 和提取 ART 的所需本地函数。

```c++
bool Init(JNIEnv *env,
          const InitInfo &info);
```

返回初始化是否成功。如果在初始化之前或初始化失败之后调用其他 LSPlant 接口，将会发生未定义行为。

### 2. Hook

通过提供 `target_method` 和上下文对象 `hooker_object` 以及回调方法 `callback_method` 来 hook 一个 Java 方法。

+ `env` 是 JNI 环境指针。

+ `target_method` 是你想 hook 的方法的 `Method` 对象。

+ `hooker_object` 是用于存储 hook 上下文的对象。

  最常见的使用方式是将备份方法存储在其中，这样当调用 `callback_method` 时，它可以调用原始方法。另一种场景是，例如在 Xposed 框架中，多个模块可以 hook 相同的 Java 方法，`hooker_object` 可以用于存储所有回调，以允许多个模块同时工作而不会冲突。

+ `callback_method` 是一个 `Method` 对象，表示用于替换 `target_method` 的回调方法。

  每当调用 `target_method` 时，`callback_method` 将代替原始的 `target_method` 被调用。`callback_method` 的签名必须为这两个方法其一：

```java
public Object callback_method(Object receiver, Object[] args)
public Object callback_method(Object[] packedArgs)
```

  也就是说，返回类型必须是 `Object`，参数类型必须是 `Object, Object[]` 或 `Object[]`。如果签名不符合要求，将会发生未定义行为。可以通过定义 `hooker_object` 的成员变量来提供额外的信息。此方法必须是 `hooker_object` 的方法。

```c++
jobject Hook(JNIEnv *env,
             jobject target_method,
             jobject hooker_object,
             jobject callback_method);
```

返回备份方法。你可以通过反射调用它来调用原始方法。如果失败则返回 `null`。

此函数会自动为 hook 生成一个存根类。为了帮助调试，你可以通过在 `InitInfo` 中设置 `generated_*` 来设置生成类的名称、字段名称和方法名称。

此函数是线程安全的（你可以在多个线程中同时调用它），但对于同一个 `target_method` 不是原子操作。这意味着在返回之前，`UnHook` 或 `IsUnhook` 不保证能够正常工作。同样，多个线程同时调用此函数时，对于相同的 `target_method` 不保证只有一个会成功。如果你在同一个 `target_method` 上同时使用不同的 `hooker_object` 调用此函数，将会发生未定义行为。

### 3. 检查

检查一个 Java 方法是否被 LSPlant hook 了。

```c++
bool IsHooked(JNIEnv *env,
              jobject method);
```

  返回该方法是否已被 hook。

### 4. Unhook

取消一个先前已 hook 的 Java 方法。

+ `env` 是 JNI 环境指针。

+ `target_method` 是你想取消 hook 的方法的 `Method` 对象。

```c++
bool UnHook(JNIEnv *env,
            jobject target_method);
```

  返回取消 hook 是否成功。

  取消 hook 后调用备份方法（`Hook()` 的返回方法）是未定义行为。请阅读 `Hook()` 的注释以获取更多详情。

### 5. 去优化

对方法进行去优化，以避免因为内联而无法调用已 hook 的被调用方法。

+ `env` 是 JNI 环境指针。

+ `method` 是要去优化的 `Method` 对象。

  通过去优化该方法，方法将不再进行内联调用。例如，如果你 hook 了一个短方法 B，而 B 被方法 A 调用，并且你发现调用 B 的回调方法在 hook 后没有被调用，那么可能是 A 已经将 B 内联到方法体中了。为了强制 A 调用你 hook 的 B，你可以去优化 A，这样你的 hook 就能生效。通常，你需要找到所有调用你 hook 的被调用方法的地方，这通常很难实现。如果你确定去优化的调用者正是你需要的，可以使用此功能。否则，最好改变 hook 点，或者手动去优化整个应用（通过简单地重新安装应用而不卸载）。

```c++
bool Deoptimize(JNIEnv *env,
                jobject method);
```

返回去优化是否成功。

对已 hook 方法调用去优化是安全的，因为去优化将作用于备份方法。

## 致谢
灵感来源于以下框架：
- [YAHFA](https://github.com/PAGalaxyLab/YAHFA)
- [SandHook](https://github.com/asLody/SandHook)
- [Pine](https://github.com/canyie/pine)
- [Epic](https://github.com/tiann/epic)

你可以通过提交 PR 或向原项目捐赠来支持开发。
