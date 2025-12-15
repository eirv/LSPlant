#include "common.hpp"

// includes
#include "art/base/arena_allocator.hpp"
#include "art/mirror/class.hpp"
#include "art/mirror/unsafe.hpp"
#include "art/runtime/art_method.hpp"
#include "art/runtime/class_linker.hpp"
#include "art/runtime/dex_file.hpp"
#include "art/runtime/gc/scoped_gc_critical_section.hpp"
#include "art/runtime/instrumentation.hpp"
#include "art/runtime/jit/jit.hpp"
#include "art/runtime/jit/jit_code_cache.hpp"
#include "art/runtime/jni/jni_id_manager.hpp"
#include "art/runtime/runtime.hpp"
#include "art/runtime/stack.hpp"
#include "art/runtime/thread.hpp"
#include "art/runtime/thread_list.hpp"
#include "dex_builder.h"
#include "reflection.hpp"

// source
#include "../lsplant.cc"
