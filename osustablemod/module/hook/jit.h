#pragma once
// hooks the JIT compiler so method bodies can be rewritten before they are compiled
// the compiler object is reached through the single exported getJit entry point
// only one vtable slot is replaced, no byte of compiled code is touched

#include <Windows.h>

#include "pe.h"
#include "imports.h"
#include "../il/patch.h"
#include "../features/approach_rate.h"

namespace jit {

  // leading fields of CORINFO_METHOD_INFO, the rest of the struct is unused here
  struct method_info {
    void* method_handle;    // +0x00
    void* module_handle;    // +0x04
    unsigned char* il;      // +0x08
    unsigned int il_size;   // +0x0C
  };

  // the compiler entry is a plain stdcall target that receives six stack arguments
  // (compiler, jit info, method info, and the three results) and reads neither ecx nor edx
  // forwarding both registers unchanged keeps the hook transparent for the caller
  using compile_method_t = int(__fastcall*)(void* ecx, void* edx, void* compiler, void* jit_info,
                                            void* method_info_ptr, void* a4, void* a5, void* a6);

  inline compile_method_t g_compile_method = nullptr;
  inline void* g_jit_object = nullptr;
  inline bool g_installed = false;
  inline volatile long g_compiled_count = 0;

  inline int __fastcall compile_hook(void* ecx, void* edx, void* compiler, void* jit_info,
                                     void* method_info_ptr, void* a4, void* a5, void* a6) {
    InterlockedIncrement(&g_compiled_count);

    if (method_info_ptr) {
      __try {
        auto* method = (method_info*)method_info_ptr;
        il_patch::apply(method->method_handle, method->il, method->il_size);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }

    int result = g_compile_method(ecx, edx, compiler, jit_info, method_info_ptr, a4, a5, a6);

    // the beatmap parser only exists as compiled code, catch it right after it
    // was produced instead of scanning the whole address space later
    if (a5) {
      __try {
        void* native_entry = *(void**)a5;
        ar_override::on_compiled_code(native_entry, 0);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }

    return result;
  }

  // replaces the compileMethod entry of the compiler vtable
  inline bool install() {
    if (g_installed)
      return true;

    static constexpr auto h_clrjit = pe::hash("clrjit.dll");
    static constexpr auto h_get_jit = pe::hash("getJit");

    void* base = pe::mod(h_clrjit);
    if (!base)
      return false;

    auto get_jit = (void*(__stdcall*)())pe::exp(base, h_get_jit);
    if (!get_jit)
      return false;

    g_jit_object = get_jit();
    if (!g_jit_object)
      return false;

    void** vtable = *(void***)g_jit_object;
    if (!vtable || !vtable[0])
      return false;

    DWORD old = 0;
    if (!imports::pVirtualProtect(&vtable[0], sizeof(void*), PAGE_READWRITE, &old))
      return false;

    g_compile_method = (compile_method_t)vtable[0];
    vtable[0] = (void*)compile_hook;

    imports::pVirtualProtect(&vtable[0], sizeof(void*), old, &old);

    g_installed = true;
    return true;
  }

}  // namespace jit
