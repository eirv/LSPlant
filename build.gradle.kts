plugins {
    alias(libs.plugins.lsplugin.publish)
}

val androidTargetSdkVersion by extra(35)
val androidMinSdkVersion by extra(21)
val androidBuildToolsVersion by extra("35.0.0")
val androidCompileSdkVersion by extra(35)
val androidNdkVersion by extra(libs.versions.ndk.get())
val androidCmakeVersion by extra("3.28.0+")
val androidAbiFilters by extra(arrayOf("arm64-v8a", "armeabi-v7a", "x86", "x86_64", "riscv64"))
