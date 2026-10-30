#pragma once
// minimal IL reader for the CLR method bodies osu! compiles at runtime
// only the opcode stream is decoded, operands are skipped by their declared size
// the tables below are generated from System.Reflection.Emit.OpCodes (ECMA-335)

#include <cstdint>

namespace il {

  // operand byte counts for one byte opcodes
  // 0xFF marks an unused opcode and 0xFE marks the switch instruction
  static constexpr unsigned char k_operand_size[256] = {
      0,    0,    0, 0, 0, 0,    0, 0, 0, 0,    0,    0,    0,    0,    1,    1,
      1,    1,    1, 1, 0, 0,    0, 0, 0, 0,    0,    0,    0,    0,    0,    1,
      4,    8,    4, 8, 0xFF, 0, 0, 4, 4, 4,    0,    1,    1,    1,    1,    1,
      1,    1,    1, 1, 1, 1,    1, 1, 4, 4,    4,    4,    4,    4,    4,    4,
      4,    4,    4, 4, 4, 0xFE, 0, 0, 0, 0,    0,    0,    0,    0,    0,    0,
      0,    0,    0, 0, 0, 0,    0, 0, 0, 0,    0,    0,    0,    0,    0,    0,
      0,    0,    0, 0, 0, 0,    0, 0, 0, 0,    0,    0,    0,    0,    0,    4,
      4,    4,    4, 4, 4, 4,    0, 0xFF, 0xFF, 4, 0,  4,    4,    4,    4,    4,
      4,    4,    0, 0, 0, 0,    0, 0, 0, 0,    0,    0,    4,    4,    0,    4,
      0,    0,    0, 0, 0, 0,    0, 0, 0, 0,    0,    0,    0,    0,    0,    0,
      0,    0,    0, 4, 4, 4,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0,    0,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 4, 0, 0xFF, 0xFF, 4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      4,    0,    0, 0, 0, 0,    0, 0, 0, 0,    0,    0,    0,    4,    1,    0,
      0,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };

  // operand byte counts for the 0xFE prefixed opcodes
  static constexpr unsigned char k_operand_size_fe[32] = {
      0,    0,    0, 0, 0, 0, 4, 4,
      0xFF, 2,    2, 2, 2, 2, 2, 0,
      0xFF, 0,    1, 0, 0, 4, 4, 0,
      0,    0xFF, 0, 0xFF, 4, 0, 0, 0xFF,
  };

  // opcodes referenced by the patches
  namespace op {
    static constexpr uint16_t nop = 0x00;
    static constexpr uint16_t ldarg_0 = 0x02;
    static constexpr uint16_t ldarg_1 = 0x03;
    static constexpr uint16_t ldloc_0 = 0x06;
    static constexpr uint16_t stloc_1 = 0x0B;
    static constexpr uint16_t stloc_2 = 0x0C;
    static constexpr uint16_t ldc_i4_0 = 0x16;
    static constexpr uint16_t ldc_i4_1 = 0x17;
    static constexpr uint16_t ldc_i4_8 = 0x1D;
    static constexpr uint16_t ldc_i4_s = 0x1F;
    static constexpr uint16_t ldc_i4 = 0x20;
    static constexpr uint16_t ldc_r4 = 0x22;
    static constexpr uint16_t dup = 0x25;
    static constexpr uint16_t pop = 0x26;
    static constexpr uint16_t call = 0x28;
    static constexpr uint16_t ret = 0x2A;
    static constexpr uint16_t br_s = 0x2B;
    static constexpr uint16_t brtrue_s = 0x2D;
    static constexpr uint16_t ble_s = 0x31;
    static constexpr uint16_t bne_un = 0x40;
    static constexpr uint16_t br = 0x38;
    static constexpr uint16_t brfalse = 0x39;
    static constexpr uint16_t brtrue = 0x3A;
    static constexpr uint16_t add = 0x58;
    static constexpr uint16_t and_ = 0x5F;
    static constexpr uint16_t callvirt = 0x6F;
    static constexpr uint16_t newobj = 0x73;
    static constexpr uint16_t ldfld = 0x7B;
    static constexpr uint16_t ldsfld = 0x7E;
    static constexpr uint16_t stsfld = 0x80;
    static constexpr uint16_t newarr = 0x8D;
    static constexpr uint16_t unbox_any = 0xA5;
    static constexpr uint16_t ldftn = 0xFE06;
    static constexpr uint16_t ceq = 0xFE01;
    static constexpr uint16_t cgt = 0xFE02;
    static constexpr uint16_t clt = 0xFE04;
  }  // namespace op

  struct insn {
    uint16_t op;
    int offset;
    int length;
    int operand;
  };

  // walks an IL body one instruction at a time
  // malformed or unsupported encodings stop the walk instead of reading past the body
  struct reader {
    const unsigned char* code;
    int size;
    int pos;
    bool valid;

    reader(const unsigned char* bytes, int length) : code(bytes), size(length), pos(0), valid(true) {}

    bool next(insn& out) {
      if (!valid || pos >= size)
        return false;

      int start = pos;
      unsigned char first = code[pos++];

      if (first == 0xFE) {
        if (pos >= size) {
          valid = false;
          return false;
        }
        unsigned char second = code[pos++];
        if (second >= 32) {
          valid = false;
          return false;
        }
        unsigned char operand = k_operand_size_fe[second];
        if (operand == 0xFF) {
          valid = false;
          return false;
        }
        if (pos + operand > size) {
          valid = false;
          return false;
        }
        out.op = (uint16_t)(0xFE00 | second);
        out.operand = pos;
        pos += operand;
        out.offset = start;
        out.length = pos - start;
        return true;
      }

      unsigned char operand = k_operand_size[first];
      if (operand == 0xFF) {
        valid = false;
        return false;
      }
      if (operand == 0xFE) {
        // switch: int32 count followed by that many int32 branch targets
        if (pos + 4 > size) {
          valid = false;
          return false;
        }
        int count = *(const int*)(code + pos);
        if (count < 0 || count > 0x10000) {
          valid = false;
          return false;
        }
        pos += 4;
        if (pos + count * 4 > size) {
          valid = false;
          return false;
        }
        pos += count * 4;
      } else {
        if (pos + operand > size) {
          valid = false;
          return false;
        }
        pos += operand;
      }

      out.op = first;
      out.operand = start + 1;
      out.offset = start;
      out.length = pos - start;
      return true;
    }
  };

  // reads the next instruction without consuming it
  inline bool peek(reader& r, insn& out) {
    int saved = r.pos;
    bool ok = r.next(out);
    r.pos = saved;
    if (!ok)
      r.valid = true;
    return ok;
  }

  inline float read_float(const unsigned char* code, int offset) {
    return *(const float*)(code + offset);
  }

  inline void write_float(unsigned char* code, int offset, float value) {
    *(float*)(code + offset) = value;
  }

  // true when the body contains the opcode sequence somewhere
  inline bool contains(const unsigned char* code, int size, const uint16_t* signature, int count) {
    reader r(code, size);
    int matched = 0;
    insn current;

    while (r.next(current)) {
      if (current.op == signature[matched]) {
        if (++matched == count)
          return true;
      } else {
        matched = current.op == signature[0] ? 1 : 0;
      }
    }

    return false;
  }

}  // namespace il
