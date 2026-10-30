#pragma once
// overrides the approach rate of every beatmap from a text file next to osu!.exe
//
// the value is written in two places:
// - while the beatmap parser stores its own approach rate, so freshly parsed
//   beatmaps already carry the wanted value
// - right before a play initializes its hit object manager, so retrying a map
//   picks up edits of the file without restarting the game

namespace ar_override {

  namespace config {
    // file name in the osu! directory, its first number is the approach rate
    inline const char* file_name = "ar.txt";
    // accepted range, values outside are clamped
    inline float minimum = 0.0f;
    inline float maximum = 11.0f;
    // how often the file is checked, in milliseconds
    inline unsigned int poll_ms = 150;
    // field offset of the approach rate inside the managed beatmap object
    static constexpr int k_approach_rate_offset = 0x2C;
  }  // namespace config

  // starts the file watcher, call once after the imports are resolved
  void initialize();

  // scans freshly compiled jit code for the beatmap parser and hooks it
  // size 0 means the scan derives its bounds from the memory region
  void on_compiled_code(void* code, unsigned int size);

  // writes the current value into the parsed beatmap before a play starts
  void before_map_load(void* player);

  // current value, or a negative number when no valid value was read yet
  float current_value();

}  // namespace ar_override
