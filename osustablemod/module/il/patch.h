#pragma once
// rewrites the IL of a few osu! methods while the CLR compiles them
// every patch keeps the instruction boundaries intact, unused bytes become nop

namespace il_patch {

  // recompile after changing these values
  namespace config {
    // draws miss markers while the Relax mod is active
    inline bool draw_misses = true;
    // plays the combo break sound while the Relax mod is active
    inline bool combo_break_sound = true;
    // lets a Relax score fail through the regular failed state
    inline bool allow_failing = true;
    // stores Relax scores to the local leaderboard on the ranking screen
    inline bool save_relax_scores = true;
    // keeps the active mods overlay visible during gameplay
    // disabled: the overlay sprite of the current osu! build is an empty quad that
    // renders as a solid black box in the bottom right corner of the playfield
    inline bool show_mods = false;
    // alpha the mods overlay fades to, 0..1
    inline float mods_overlay_alpha = 0.20f;
    // raises the darkening overlay on song select thumbnails
    inline bool thumbnail_alpha = true;
    // 0..255, the overlay is drawn with this alpha while the item is deselected
    inline unsigned char thumbnail_deselected_alpha = 185;
    // stops the thumbnail from fading in from zero on every update
    inline bool thumbnail_skip_fade_in = true;
  }  // namespace config

  enum patch_id : unsigned int {
    k_draw_misses = 1u << 0,
    k_combo_break_sound = 1u << 1,
    k_allow_failing = 1u << 2,
    k_save_relax_scores = 1u << 3,
    k_show_mods = 1u << 4,
    k_thumbnail_alpha = 1u << 5,
    k_thumbnail_fade = 1u << 6,
  };

  // applies every enabled patch to a method body
  // method identifies the compiled method and il points at its IL bytes
  // returns the patches that changed the body
  unsigned int apply(void* method, unsigned char* il, unsigned int size);

  // name of a single patch id, used for logging
  const char* name(patch_id id);

}  // namespace il_patch
