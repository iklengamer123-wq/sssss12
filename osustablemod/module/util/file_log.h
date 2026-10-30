#pragma once
// appends a small text log next to osu!.exe so patched methods can be verified
// the hook has no CRT, therefore every string is formatted by hand

#include <Windows.h>

#include "../hook/imports.h"

namespace file_log {

  inline HANDLE g_file = INVALID_HANDLE_VALUE;

  struct builder {
    char data[320];
    int length;

    builder() : length(0) {}

    void character(char value) {
      if (length < (int)sizeof(data))
        data[length++] = value;
    }

    void text(const char* value) {
      while (*value)
        character(*value++);
    }

    void number(unsigned int value, int width = 0) {
      char digits[11];
      int count = 0;
      do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
      } while (value && count < 10);

      for (int i = count; i < width; i++)
        character('0');
      while (count > 0)
        character(digits[--count]);
    }
  };

  inline void write_bytes(const char* data, unsigned int length) {
    if (g_file == INVALID_HANDLE_VALUE || !length)
      return;
    DWORD written = 0;
    imports::pWriteFile(g_file, data, length, &written, nullptr);
  }

  inline void line(const char* message) {
    if (g_file == INVALID_HANDLE_VALUE)
      return;

    SYSTEMTIME now = {};
    imports::pGetLocalTime(&now);

    builder out;
    out.character('[');
    out.number(now.wYear, 4);
    out.character('-');
    out.number(now.wMonth, 2);
    out.character('-');
    out.number(now.wDay, 2);
    out.character(' ');
    out.number(now.wHour, 2);
    out.character(':');
    out.number(now.wMinute, 2);
    out.character(':');
    out.number(now.wSecond, 2);
    out.text("] ");
    out.text(message);
    out.text("\r\n");

    write_bytes(out.data, (unsigned int)out.length);
  }

  inline void patched(const char* patch, unsigned int size) {
    builder out;
    out.text("patched ");
    out.text(patch);
    out.text(" (il ");
    out.number(size);
    out.text(" bytes)");
    out.character(0);
    line(out.data);
  }

  inline void detail(const char* label, unsigned int value) {
    builder out;
    out.text(label);
    out.text(" ");
    out.number(value);
    out.character(0);
    line(out.data);
  }

  // opens <osu! directory>/relax-patches.log for appending
  inline bool init() {
    if (g_file != INVALID_HANDLE_VALUE)
      return true;

    if (!imports::pGetModuleFileNameA || !imports::pCreateFileA || !imports::pWriteFile ||
        !imports::pGetLocalTime)
      return false;

    char path[MAX_PATH];
    DWORD length = imports::pGetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
      return false;

    int directory = (int)length;
    while (directory > 0 && path[directory - 1] != '\\')
      directory--;
    if (directory <= 0 || directory >= MAX_PATH - 24)
      return false;

    static constexpr char k_name[] = "relax-patches.log";
    int name_length = (int)sizeof(k_name);
    if (directory + name_length > MAX_PATH)
      return false;

    for (int i = 0; i < name_length; i++)
      path[directory + i] = k_name[i];

    g_file = imports::pCreateFileA(path, FILE_APPEND_DATA,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
      g_file = INVALID_HANDLE_VALUE;
      return false;
    }

    line("initializing");
    return true;
  }

}  // namespace file_log
