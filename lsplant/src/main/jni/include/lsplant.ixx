module;

#include "lsplant.hpp"

export module lsplant;

export namespace lsplant::inline v3 {
using lsplant::v3::Deoptimize;
using lsplant::v3::GetNativeFunction;
using lsplant::v3::Hook;
using lsplant::v3::HookResult;
using lsplant::v3::HookUsingNativeAPI;
using lsplant::v3::Init;
using lsplant::v3::InitInfo;
using lsplant::v3::InvokeSpecial;
using lsplant::v3::IsHooked;
using lsplant::v3::MakeClassHidden;
using lsplant::v3::MakeClassInheritable;
using lsplant::v3::MakeClassVisible;
using lsplant::v3::MakeDexFileTrusted;
using lsplant::v3::MakeMethodHidden;
using lsplant::v3::NativeCallbackType;
using lsplant::v3::OpenInMemoryDexFile;
using lsplant::v3::SetHookEnabled;
using lsplant::v3::UnHook;
}  // namespace lsplant::inline v3
