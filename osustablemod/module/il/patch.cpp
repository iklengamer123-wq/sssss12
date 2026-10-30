#include "patch.h"

#include "il.h"
#include "../hook/imports.h"
#include "../hook/nocrt.h"
#include "../util/file_log.h"

namespace il_patch {

  namespace {

    // method bodies larger than this are never patched
    static constexpr unsigned int k_max_body_size = 0x10000;

    // opcode sequences that identify the methods which need a rewrite
    // the leading instructions of each osu! method, order is part of the identity
    static constexpr uint16_t k_gate_onload[] = {
        0x38, 0x11, 0x6F, 0xA5, 0x0C, 0x7E, 0x7B, 0x28,
    };
    static constexpr uint16_t k_gate_tree_item_update[] = {
        0x26, 0x7E, 0xFE06, 0x73, 0x25, 0x80, 0x6F, 0x2A, 0x02, 0x7B, 0x06, 0xFE06, 0x73,
    };
    static constexpr uint16_t k_gate_tree_item_populate[] = {
        0x39, 0x7E, 0x7B, 0x20, 0xFE04, 0x16, 0xFE01, 0x13, 0x02, 0x1B, 0x8D, 0x25,
    };
    static constexpr uint16_t k_gate_thumbnail_fade[] = {
        0x02, 0x7B, 0x06, 0x7B, 0x2D, 0x20, 0x2B, 0x16, 0x6F, 0x26,
    };

    // prefixes of the relax checks, the condition after the prefix is removed
    // draw misses: if (increaseScoreType == -131072 && !Player.Relaxing && !Player.Relaxing2)
    static constexpr uint16_t k_sig_draw_misses[] = {
        0x03, 0x1E, 0x6F, 0x13, 0x06, 0x20, 0x40, 0x7E, 0x3A, 0x7E, 0x3A,
    };
    // combo break sound: if (HitCombo > 20 && !Player.Relaxing && !Player.Relaxing2)
    static constexpr uint16_t k_sig_combo_break[] = {
        0x02, 0x7B, 0x6F, 0x1F, 0x31, 0x7E, 0x2D, 0x7E, 0x2D,
    };
    // failing: if ((mods & Mods.NoFail) <= Mods.None && !Player.Relaxing && !Player.Relaxing2)
    static constexpr uint16_t k_sig_allow_failing[] = {
        0x5F, 0x16, 0xFE02, 0x2D, 0x7E, 0x2D, 0x7E, 0x2D,
    };
    // local leaderboard: the two guard blocks that reject Relax scores
    static constexpr uint16_t k_sig_save_scores[] = {
        0x02, 0x7B, 0x7B, 0x39, 0x02, 0x7B, 0x3A, 0x7E, 0x7B, 0x28, 0x20, 0x0C,
        0x0B, 0x07, 0x08, 0x5F, 0x16, 0xFE02, 0x3A, 0x7E, 0x7B, 0x28, 0x20, 0x0C,
        0x0B, 0x07, 0x08, 0x5F, 0x16, 0xFE02, 0x3A,
    };

    // methods rewritten so far, a body is only patched once per process
    void* g_patched_methods[256] = {};
    volatile long g_patched_count = 0;

    bool already_patched(void* method) {
      long count = g_patched_count;
      for (long i = 0; i < count && i < 256; i++) {
        if (g_patched_methods[i] == method)
          return true;
      }
      return false;
    }

    void remember(void* method) {
      long index = InterlockedIncrement(&g_patched_count) - 1;
      if (index >= 0 && index < 256)
        g_patched_methods[index] = method;
    }

    // replaces whole instructions with nop, the body length stays unchanged
    void noop_range(unsigned char* code, int offset, int length) {
      nc_memset(code + offset, 0, (unsigned int)length);
    }

    // matches a whole signature, then turns its last instructions into nop
    // the relax guards keep their leading comparison and lose the two flag tests
    bool noop_tail(unsigned char* code, int size, const uint16_t* signature, int length, int tail) {
      if (tail <= 0 || tail > length)
        return false;

      il::reader r(code, size);
      il::insn current;
      int matched = 0;
      int match_start = -1;

      while (r.next(current)) {
        if (current.op == signature[matched]) {
          if (matched == 0)
            match_start = current.offset;
          matched++;

          if (matched == length) {
            il::reader walk(code, size);
            walk.pos = match_start;

            for (int i = 0; i < length - tail; i++) {
              il::insn skipped;
              if (!walk.next(skipped))
                return false;
            }

            for (int i = 0; i < tail; i++) {
              il::insn target;
              if (!walk.next(target))
                return false;
              noop_range(code, target.offset, target.length);
            }

            return true;
          }
        } else if (current.op == signature[0]) {
          matched = 1;
          match_start = current.offset;
        } else {
          matched = 0;
          match_start = -1;
        }
      }

      return false;
    }

    // turns an exact opcode sequence into nop
    bool noop_sequence(unsigned char* code, int size, const uint16_t* signature, int count) {
      il::reader r(code, size);
      il::insn current;

      while (r.next(current)) {
        int start = current.offset;

        il::reader probe(code, size);
        probe.pos = start;
        il::insn probe_insn;
        int matched = 0;
        while (matched < count) {
          if (!probe.next(probe_insn) || probe_insn.op != signature[matched])
            break;
          matched++;
        }

        if (matched == count) {
          il::reader walk(code, size);
          walk.pos = start;
          for (int i = 0; i < count; i++) {
            il::insn target;
            if (!walk.next(target))
              return false;
            noop_range(code, target.offset, target.length);
          }
          return true;
        }
      }

      return false;
    }

    // rewrites every ldc.i4.s constant with the given value inside the body
    bool rewrite_i4_s(unsigned char* code, int size, unsigned char from, unsigned char to) {
      il::reader r(code, size);
      il::insn current;
      bool changed = false;

      while (r.next(current)) {
        if (current.op != il::op::ldc_i4_s)
          continue;
        if (code[current.operand] != from)
          continue;
        code[current.operand] = to;
        changed = true;
      }

      return changed;
    }

    // replaces the mods overlay setup of Player.OnLoadComplete
    // the sprite is created with alwaysDraw = ReplayMode, then faded to zero
    bool patch_mods_overlay(unsigned char* code, int size) {
      enum { find_constant, replace_always_draw, replace_fade_end, finished } state = find_constant;
      il::reader r(code, size);
      il::insn current;

      while (r.next(current)) {
        switch (state) {
          case find_constant:
            if (current.op == il::op::ldc_r4 &&
                il::read_float(code, current.operand) == 94.0f)
              state = replace_always_draw;
            break;

          case replace_always_draw:
            if (current.op == il::op::call && current.length >= 2) {
              // load true for the alwaysDraw parameter, the rest of the call becomes nop
              code[current.offset] = (unsigned char)il::op::ldc_i4_1;
              noop_range(code, current.offset + 1, current.length - 1);
              state = replace_fade_end;
            }
            break;

          case replace_fade_end:
            if (current.op == il::op::ldc_r4 &&
                il::read_float(code, current.operand) == 0.0f) {
              il::write_float(code, current.operand, config::mods_overlay_alpha);
              return true;
            }
            break;

          default:
            break;
        }
      }

      return false;
    }

    // records every patch that changed a body
    void log_applied(unsigned int applied, unsigned int size) {
      for (unsigned int bit = k_draw_misses; bit <= k_thumbnail_fade; bit <<= 1) {
        if (applied & bit)
          file_log::patched(name((patch_id)bit), size);
      }
    }

  }  // namespace

  unsigned int apply(void* method, unsigned char* il, unsigned int size) {
    if (!il || !size || size > k_max_body_size)
      return 0;
    if (method && already_patched(method))
      return 0;

    HANDLE heap = imports::pGetProcessHeap();
    if (!heap)
      return 0;

    auto* copy = (unsigned char*)imports::pHeapAlloc(heap, 0, size);
    if (!copy)
      return 0;
    nc_memcpy(copy, il, size);

    int length = (int)size;
    unsigned int applied = 0;

    if (config::draw_misses && noop_tail(copy, length, k_sig_draw_misses, 11, 4))
      applied |= k_draw_misses;

    if (config::combo_break_sound && noop_tail(copy, length, k_sig_combo_break, 9, 4))
      applied |= k_combo_break_sound;

    if (config::allow_failing && noop_tail(copy, length, k_sig_allow_failing, 8, 4))
      applied |= k_allow_failing;

    if (config::save_relax_scores && noop_tail(copy, length, k_sig_save_scores, 31, 24))
      applied |= k_save_relax_scores;

    if (config::show_mods && il::contains(copy, length, k_gate_onload, 8) &&
        patch_mods_overlay(copy, length))
      applied |= k_show_mods;

    if (config::thumbnail_alpha &&
        (il::contains(copy, length, k_gate_tree_item_update, 13) ||
         il::contains(copy, length, k_gate_tree_item_populate, 12)) &&
        rewrite_i4_s(copy, length, 50, config::thumbnail_deselected_alpha))
      applied |= k_thumbnail_alpha;

    if (config::thumbnail_skip_fade_in &&
        il::contains(copy, length, k_gate_thumbnail_fade, 10) &&
        noop_sequence(copy, length, k_gate_thumbnail_fade, 10))
      applied |= k_thumbnail_fade;

    if (applied) {
      DWORD old = 0;
      if (imports::pVirtualProtect(il, size, PAGE_EXECUTE_READWRITE, &old)) {
        nc_memcpy(il, copy, size);
        imports::pVirtualProtect(il, size, old, &old);
        if (method)
          remember(method);
        log_applied(applied, size);
      } else {
        applied = 0;
      }
    }

    imports::pHeapFree(heap, 0, copy);
    return applied;
  }

  const char* name(patch_id id) {
    switch (id) {
      case k_draw_misses:
        return "AllowRelaxDrawMisses";
      case k_combo_break_sound:
        return "AllowRelaxComboBreakSound";
      case k_allow_failing:
        return "AllowRelaxFailing";
      case k_save_relax_scores:
        return "AutoSaveRelaxScores";
      case k_show_mods:
        return "ShowModsInGameplay";
      case k_thumbnail_alpha:
        return "CustomSongSelectThumbnailAlpha";
      case k_thumbnail_fade:
        return "DisableThumbnailFadeIn";
      default:
        return "UnknownPatch";
    }
  }

}  // namespace il_patch
