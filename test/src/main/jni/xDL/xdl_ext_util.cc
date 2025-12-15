#include "xdl_util.h"

#define xdl_util_get_api_level __xdl_util_get_api_level
#include "xdl_util.c"
#undef xdl_util_get_api_level

[[gnu::always_inline, gnu::const]] int xdl_util_get_api_level() {
  static auto api_level = [] { return android_get_device_api_level(); }();
  [[assume(api_level >= __ANDROID_API__)]];
  return api_level;
}
