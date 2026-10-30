#pragma once
// adjusts cursor input through the game's SetMousePos delegate

#include "../game/math.hpp"
#include "../hook/nocrt.h"

namespace aa {

  // recompile after changing these values
  namespace config {
    // maximum cursor offset in osu! pixels (scaled by playfield scale)
    inline float max_offset = 24.0f;
    // how strongly the offset grows per frame (0..1)
    inline float assist_strength = 0.70f;
    // extra catch up pull while the cursor is outside the circle (0..1)
    inline float range_pull = 1.00f;
    // resync speed when outside assist range (0..1)
    inline float power = 0.50f;
    // how fast the offset returns to zero outside range
    inline float resync_speed = 0.35f;
    // decay near the circle center (1 = no decay, 0 = full stop at center)
    inline float deadzone_decay = 0.80f;
    // deadzone inner radius relative to object radius
    inline float deadzone_radius = 0.50f;
    // reduce pull when the cursor is already inside the circle (anti-tug)
    inline bool enable_anti_tug = true;
    // how much of the pull is removed while tapping (0..1)
    inline float anti_tug_damping = 0.85f;
    // stream detection thresholds
    inline float stream_time_threshold = 125.0f;
    inline float stream_distance_mult = 5.0f;
    inline float stream_velocity_cap = 0.50f;
    // slider head attraction multiplier (0 = ignore sliders)
    inline float slider_head_mult = 1.0f;
    // target switch smoothing time in seconds
    inline float target_switch_blend = 0.03f;
  }  // namespace config

  struct state {
    vec2 target_pos, mouse_offset, prev_mouse_pos;
    vec2 prev_offset_delta;
    vec2 prev_output_vel;
    vec2 smooth_target;           // blended target position
    bool should_assist, in_gameplay;
    bool is_slider_target;        // current target is a slider head
    float object_radius, assist_radius, max_offset, current_decay;
    double current_target_time;
    float tap_engagement, stream_factor;
    float angle_noise_assist, angle_noise_resync;
    float speed_noise;
    float hesitation;
    float resync_blend;
    float switch_blend;           // 0..1, 1 = fully on new target
    int64_t last_step_ticks, qpc_freq_ticks;
  };

  extern state g;

  void begin_gameplay(vec2 cursor);
  void reset();
  bool needs_resync();
  void set_target(vec2 sp, float rad, double st, float scale, bool slider = false);
  bool check_range(vec2 c);
  vec2 process(vec2 raw);

}  // namespace aa
