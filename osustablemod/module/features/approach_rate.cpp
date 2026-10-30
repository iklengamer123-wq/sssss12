#include "approach_rate.h"

#include "../hook/imports.h"
#include "../hook/nocrt.h"
#include "../util/file_log.h"

namespace ar_override {

  namespace {

    // shared with the generated stubs, the stub reads and writes these directly
    alignas(4) static float g_value = 0.0f;
    alignas(4) static int g_valid = 0;
    alignas(4) static void* g_beatmap = nullptr;
    alignas(4) static void* g_beatmap_method_table = nullptr;

    static char g_path[MAX_PATH] = {};
    static bool g_path_ready = false;

    static unsigned char* g_stub_page = nullptr;
    static int g_stub_used = 0;
    static constexpr int k_stub_page_size = 0x1000;

    static float g_last_applied = -1.0f;

    static void* g_hooked_sites[8] = {};
    static int g_hooked_count = 0;

    // the parser stores the approach rate with these two instruction shapes
    //   8B 85 xx xx xx xx     mov eax, [ebp+disp]
    //   D9 58 2C              fstp dword ptr [eax+0x2C]
    //   8B 85 xx xx xx xx     mov eax, [ebp+disp]
    //   D9 40 38              fld dword ptr [eax+0x38]
    //   D9 58 2C              fstp dword ptr [eax+0x2C]
    bool is_single_store(const unsigned char* p) {
      return p[0] == 0x8B && p[1] == 0x85 && p[6] == 0xD9 && p[7] == 0x58 && p[8] == 0x2C;
    }

    bool is_offset_store(const unsigned char* p) {
      return p[0] == 0x8B && p[1] == 0x85 && p[6] == 0xD9 && p[7] == 0x40 && p[8] == 0x38 &&
             p[9] == 0xD9 && p[10] == 0x58 && p[11] == 0x2C;
    }

    bool already_hooked(const void* site) {
      for (int i = 0; i < g_hooked_count; i++)
        if (g_hooked_sites[i] == site)
          return true;
      return false;
    }

    void emit_bytes(unsigned char*& cursor, const unsigned char* bytes, int count) {
      for (int i = 0; i < count; i++)
        *cursor++ = bytes[i];
    }

    unsigned char* allocate_stub() {
      if (!g_stub_page) {
        g_stub_page = (unsigned char*)imports::pVirtualAlloc(nullptr, k_stub_page_size,
                                                             MEM_COMMIT | MEM_RESERVE,
                                                             PAGE_EXECUTE_READWRITE);
        g_stub_used = 0;
      }
      if (!g_stub_page || g_stub_used + 96 > k_stub_page_size)
        return nullptr;

      unsigned char* stub = g_stub_page + g_stub_used;
      g_stub_used += 96;
      return stub;
    }

    // builds a stub that repeats the stolen instructions, remembers the beatmap
    // object and stores the wanted approach rate unless no value was read yet
    bool install_site(unsigned char* site, int copy_length, int frame_offset, bool with_load) {
      if (already_hooked(site))
        return true;

      unsigned char* stub = allocate_stub();
      if (!stub)
        return false;

      unsigned char* cursor = stub;

      // mov eax, [ebp+frame_offset]
      cursor[0] = 0x8B;
      cursor[1] = 0x85;
      *(int*)(cursor + 2) = frame_offset;
      cursor += 6;

      // fld dword ptr [eax+0x38]
      if (with_load) {
        cursor[0] = 0xD9;
        cursor[1] = 0x40;
        cursor[2] = 0x38;
        cursor += 3;
      }

      // fstp dword ptr [eax+0x2C]
      cursor[0] = 0xD9;
      cursor[1] = 0x58;
      cursor[2] = 0x2C;
      cursor += 3;

      // mov [g_beatmap], eax
      cursor[0] = 0x89;
      cursor[1] = 0x05;
      *(void**)(cursor + 2) = (void*)&g_beatmap;
      cursor += 6;

      // the method table identifies the object again after a garbage collection
      // mov edx, [eax]
      cursor[0] = 0x8B;
      cursor[1] = 0x10;
      cursor += 2;
      // mov [g_beatmap_method_table], edx
      cursor[0] = 0x89;
      cursor[1] = 0x15;
      *(void**)(cursor + 2) = (void*)&g_beatmap_method_table;
      cursor += 6;

      // mov edx, [g_value]
      cursor[0] = 0x8B;
      cursor[1] = 0x15;
      *(void**)(cursor + 2) = (void*)&g_value;
      cursor += 6;

      // cmp dword ptr [g_valid], 0
      cursor[0] = 0x83;
      cursor[1] = 0x3D;
      *(void**)(cursor + 2) = (void*)&g_valid;
      cursor += 6;
      *cursor++ = 0x00;

      // je +3
      cursor[0] = 0x74;
      cursor[1] = 0x03;
      cursor += 2;

      // mov [eax+0x2C], edx
      cursor[0] = 0x89;
      cursor[1] = 0x50;
      cursor[2] = 0x2C;
      cursor += 3;

      // jmp back behind the replaced instructions
      cursor[0] = 0xE9;
      *(int*)(cursor + 1) = (int)(site + copy_length - (cursor + 5));

      DWORD old = 0;
      if (!imports::pVirtualProtect(site, copy_length, PAGE_EXECUTE_READWRITE, &old))
        return false;

      site[0] = 0xE9;
      *(int*)(site + 1) = (int)(stub - (site + 5));
      for (int i = 5; i < copy_length; i++)
        site[i] = 0x90;

      DWORD ignored = 0;
      imports::pVirtualProtect(site, copy_length, old, &ignored);

      if (g_hooked_count < 8)
        g_hooked_sites[g_hooked_count++] = site;
      return true;
    }

    // the parser is only patched when both instruction shapes are present in the
    // same jitted method, which keeps unrelated float stores untouched
    void hook_parser(unsigned char* code, unsigned int size) {
      unsigned char* single[8] = {};
      int single_count = 0;
      unsigned char* offset[4] = {};
      int offset_count = 0;

      for (unsigned int i = 0; i + 12 <= size; i++) {
        if (is_offset_store(code + i)) {
          int frame = *(int*)(code + i + 2);
          if (frame < 0 && frame > -0x8000 && offset_count < 4)
            offset[offset_count++] = code + i;
          i += 11;
          continue;
        }
        if (is_single_store(code + i)) {
          int frame = *(int*)(code + i + 2);
          if (frame < 0 && frame > -0x8000 && single_count < 8)
            single[single_count++] = code + i;
          i += 8;
        }
      }

      if (!single_count || !offset_count)
        return;

      for (int i = 0; i < single_count; i++)
        install_site(single[i], 9, *(int*)(single[i] + 2), false);
      for (int i = 0; i < offset_count; i++)
        install_site(offset[i], 12, *(int*)(offset[i] + 2), true);

      file_log::detail("approach rate parser hooked, sites:", (unsigned int)(single_count + offset_count));
    }

    bool build_path() {
      if (g_path_ready)
        return true;
      if (!imports::pGetModuleFileNameA)
        return false;

      DWORD length = imports::pGetModuleFileNameA(nullptr, g_path, MAX_PATH);
      if (!length || length >= MAX_PATH)
        return false;

      int directory = (int)length;
      while (directory > 0 && g_path[directory - 1] != '\\')
        directory--;
      if (directory <= 0)
        return false;

      int name_length = 0;
      while (config::file_name[name_length])
        name_length++;
      if (directory + name_length + 1 >= MAX_PATH)
        return false;

      for (int i = 0; i <= name_length; i++)
        g_path[directory + i] = config::file_name[i];

      g_path_ready = true;
      return true;
    }

    // the first number of the file is the approach rate, anything else on the
    // line is ignored so values like "AR: 9.5" keep working
    bool parse_number(const char* text, int length, float& out) {
      int i = 0;
      while (i < length) {
        char c = text[i];

        if (c == '#' || c == ';') {
          while (i < length && text[i] != '\n')
            i++;
          continue;
        }

        bool starts_number = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
        if (!starts_number) {
          i++;
          continue;
        }

        int start = i;
        float sign = 1.0f;
        if (text[i] == '-') {
          sign = -1.0f;
          i++;
        } else if (text[i] == '+') {
          i++;
        }

        float whole = 0.0f;
        float fraction = 0.0f;
        float divisor = 1.0f;
        bool digits = false;

        while (i < length && text[i] >= '0' && text[i] <= '9') {
          whole = whole * 10.0f + (float)(text[i] - '0');
          digits = true;
          i++;
        }

        if (i < length && text[i] == '.') {
          i++;
          while (i < length && text[i] >= '0' && text[i] <= '9') {
            fraction = fraction * 10.0f + (float)(text[i] - '0');
            divisor *= 10.0f;
            digits = true;
            i++;
          }
        }

        if (digits) {
          out = sign * (whole + fraction / divisor);
          return true;
        }

        i = start + 1;
      }

      return false;
    }

    bool read_file_value(float& out) {
      if (!build_path())
        return false;
      if (!imports::pCreateFileA || !imports::pReadFile || !imports::pGetFileSize ||
          !imports::pCloseHandle || !imports::pHeapAlloc || !imports::pHeapFree ||
          !imports::pGetProcessHeap)
        return false;

      HANDLE file = imports::pCreateFileA(g_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file == INVALID_HANDLE_VALUE)
        return false;

      DWORD size = imports::pGetFileSize(file, nullptr);
      if (!size || size > 4096) {
        imports::pCloseHandle(file);
        return false;
      }

      HANDLE heap = imports::pGetProcessHeap();
      auto* buffer = (char*)imports::pHeapAlloc(heap, 0, size + 1);
      if (!buffer) {
        imports::pCloseHandle(file);
        return false;
      }

      DWORD read = 0;
      BOOL ok = imports::pReadFile(file, buffer, size, &read, nullptr);
      imports::pCloseHandle(file);

      bool parsed = false;
      if (ok && read) {
        buffer[read] = 0;
        parsed = parse_number(buffer, (int)read, out);
      }

      imports::pHeapFree(heap, 0, buffer);
      return parsed;
    }

    // a managed object is recognised by its method table, which survives a
    // garbage collection even when the object itself was moved
    bool beatmap_alive(void* candidate) {
      if (!candidate || (unsigned int)candidate < 0x10000)
        return false;

      MEMORY_BASIC_INFORMATION region = {};
      if (!imports::pVirtualQuery(candidate, &region, sizeof(region)) ||
          region.State != MEM_COMMIT)
        return false;

      if (*(void**)candidate != g_beatmap_method_table)
        return false;

      const float* fields = (const float*)((const char*)candidate + 0x2C);
      float approach = fields[0];
      float circle = fields[1];
      float overall = fields[3];
      return approach >= -2.0f && approach <= 25.0f && circle >= 0.0f && circle <= 25.0f &&
             overall >= 0.0f && overall <= 25.0f;
    }

    void apply_to_beatmap(void* beatmap) {
      if (!g_valid || !beatmap)
        return;
      *(float*)((char*)beatmap + config::k_approach_rate_offset) = g_value;
    }

    void refresh() {
      float value = 0.0f;
      if (!read_file_value(value)) {
        g_valid = 0;
        return;
      }

      if (value < config::minimum)
        value = config::minimum;
      if (value > config::maximum)
        value = config::maximum;

      if (!g_valid || value != g_value) {
        g_value = value;
        g_valid = 1;
        file_log::detail("ar.txt value x100:", (unsigned int)(value * 100.0f));
      }
    }

    DWORD WINAPI watcher_thread(LPVOID) {
      for (;;) {
        refresh();

        // keep rewriting the parsed beatmap so a retry, which reuses that
        // object, already carries the wanted value before its play is built
        if (g_valid && g_beatmap) {
          __try {
            if (beatmap_alive(g_beatmap)) {
              apply_to_beatmap(g_beatmap);
              if (g_value != g_last_applied) {
                g_last_applied = g_value;
                file_log::detail("watcher ar x100:", (unsigned int)(g_value * 100.0f));
              }
            }
          } __except (EXCEPTION_EXECUTE_HANDLER) {
          }
        }

        imports::pSleep(config::poll_ms);
      }

      return 0;
    }

  }  // namespace

  void initialize() {
    refresh();
    imports::pCreateThread(nullptr, 0, watcher_thread, nullptr, 0, nullptr);
  }

  void on_compiled_code(void* code, unsigned int size) {
    if (!code)
      return;

    // the jit result carries no reliable size here, so the scan is bounded by
    // the committed region and a conservative cap
    if (!size) {
      MEMORY_BASIC_INFORMATION region = {};
      if (!imports::pVirtualQuery(code, &region, sizeof(region)) || region.State != MEM_COMMIT)
        return;

      unsigned char* end = (unsigned char*)region.BaseAddress + region.RegionSize;
      size = (unsigned int)(end - (unsigned char*)code);
    }

    if (size < 16)
      return;
    if (size > 0x10000)
      size = 0x10000;

    __try {
      hook_parser((unsigned char*)code, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }

  // finds the beatmap object of this play through the player fields, validated by
  // the method table captured while the beatmap was parsed
  void before_map_load(void* player) {
    if (!g_valid)
      return;

    __try {
      // prefer the object the player references, the watcher keeps it fresh
      // between retries of the same map
      if (player && g_beatmap_method_table) {
        auto** slots = (void**)player;
        for (int i = 0; i < 0x400 / 4; i++) {
          void* candidate = slots[i];
          if (!beatmap_alive(candidate))
            continue;

          if (candidate != g_beatmap) {
            g_beatmap = candidate;
            file_log::detail("beatmap located for this play:", 1);
          }

          // the value seen here tells whether the watcher already reached this
          // object before the play started
          float before = *(float*)((char*)candidate + config::k_approach_rate_offset);
          file_log::detail("beatmap ar before x100:", (unsigned int)(before * 100.0f));

          apply_to_beatmap(candidate);
          return;
        }
      }

      if (g_beatmap && beatmap_alive(g_beatmap))
        apply_to_beatmap(g_beatmap);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
  }

  float current_value() {
    return g_valid ? g_value : -1.0f;
  }

}  // namespace ar_override
