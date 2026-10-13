"""Small reverse-engineering helpers for the unmapped Minecraft dump (see dump_module.py).

Everything works on RVAs (offsets from the image base). The dump is unmapped, so a file
offset equals its RVA.
"""
import bisect
import re
import struct

import numpy as np


class Image:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        self.num_sections = struct.unpack_from("<H", self.data, e_lfanew + 6)[0]
        opt = e_lfanew + 24
        opt_size = struct.unpack_from("<H", self.data, e_lfanew + 20)[0]
        self.image_base_header = struct.unpack_from("<Q", self.data, opt + 24)[0]
        self.sections = {}
        sec = opt + opt_size
        for i in range(self.num_sections):
            off = sec + i * 40
            name = self.data[off:off + 8].rstrip(b"\0").decode(errors="replace")
            vsize, vaddr = struct.unpack_from("<II", self.data, off + 8)
            self.sections[name] = (vaddr, vsize)
        # Exception directory (.pdata) gives the bounds of every non-leaf function.
        data_dirs = opt + 112
        exc_rva, exc_size = struct.unpack_from("<II", self.data, data_dirs + 3 * 8)
        funcs = np.frombuffer(self.data, dtype="<u4", count=exc_size // 4, offset=exc_rva).reshape(-1, 3)
        self.func_begin = funcs[:, 0].copy()
        self.func_end = funcs[:, 1].copy()
        self._text_np = None

    # ---- basic reads -----------------------------------------------------
    def u8(self, rva): return self.data[rva]
    def i32(self, rva): return struct.unpack_from("<i", self.data, rva)[0]
    def u32(self, rva): return struct.unpack_from("<I", self.data, rva)[0]
    def u64(self, rva): return struct.unpack_from("<Q", self.data, rva)[0]
    def bytes(self, rva, n): return self.data[rva:rva + n]

    def cstr(self, rva, maxlen=256):
        end = self.data.find(b"\0", rva, rva + maxlen)
        return self.data[rva:end].decode(errors="replace")

    def section_of(self, rva):
        for name, (va, size) in self.sections.items():
            if va <= rva < va + size:
                return name
        return None

    # ---- functions ---------------------------------------------------------
    def func_containing(self, rva):
        """Start RVA of the function containing rva (follows chained unwind info)."""
        i = bisect.bisect_right(self.func_begin, rva) - 1
        if i < 0 or rva >= self.func_end[i]:
            return None
        return int(self.func_begin[i])

    # ---- pattern scanning --------------------------------------------------
    @staticmethod
    def compile_pattern(pattern):
        parts = []
        for tok in pattern.split():
            if tok in ("?", "??"):
                parts.append(b".")
            else:
                parts.append(re.escape(bytes([int(tok, 16)])))
        return re.compile(b"".join(parts), re.DOTALL)

    def scan(self, pattern, section=".text", limit=10):
        va, size = self.sections[section]
        rx = self.compile_pattern(pattern)
        out = []
        for m in rx.finditer(self.data, va, va + size):
            out.append(m.start())
            if len(out) >= limit:
                break
        return out

    def rip_target(self, rva, disp_offset, insn_len=None):
        """Resolve a rip-relative operand: disp32 at rva+disp_offset, next insn at rva+insn_len."""
        if insn_len is None:
            insn_len = disp_offset + 4
        return rva + insn_len + self.i32(rva + disp_offset)

    # ---- strings and cross-references --------------------------------------
    def find_bytes(self, needle, section=None):
        out = []
        lo, hi = (0, len(self.data)) if section is None else (
            self.sections[section][0], self.sections[section][0] + self.sections[section][1])
        i = self.data.find(needle, lo, hi)
        while i != -1:
            out.append(i)
            i = self.data.find(needle, i + 1, hi)
        return out

    def find_string(self, s, section=".rdata"):
        return self.find_bytes(s.encode() + b"\0", section)

    def _text(self):
        if self._text_np is None:
            va, size = self.sections[".text"]
            self._text_np = np.frombuffer(self.data, dtype=np.uint8, count=size, offset=va)
        return self._text_np

    def xrefs_rip(self, target, max_results=64):
        """RVAs of rip-relative disp32 fields in .text that point to target.

        Handles operands that end the instruction (lea/mov/call/jmp) and operands followed by
        an imm8 or imm32 (cmp [rip+x], imm).
        """
        va, size = self.sections[".text"]
        text = self._text()
        results = []
        chunk = 1 << 24
        for start in range(0, size - 4, chunk):
            end = min(start + chunk + 3, size)
            seg = text[start:end].astype(np.int64)
            n = len(seg) - 3
            disp = seg[:n] | (seg[1:n + 1] << 8) | (seg[2:n + 2] << 16) | (seg[3:n + 3] << 24)
            disp = np.where(disp >= 1 << 31, disp - (1 << 32), disp)
            pos = np.arange(n, dtype=np.int64) + va + start
            for tail in (4, 5, 8):
                hits = np.nonzero(pos + tail + disp == target)[0]
                for h in hits:
                    results.append(int(pos[h]))
            if len(results) >= max_results:
                break
        return sorted(set(results))[:max_results]

    def find_qword(self, value, section=".rdata"):
        return self.find_bytes(struct.pack("<Q", value), section)

    # ---- RTTI ----------------------------------------------------------------
    def type_descriptor(self, mangled):
        """RVA of the TypeDescriptor whose name is e.g. '.?AVClientInstance@@'."""
        hits = self.find_bytes(mangled.encode() + b"\0", ".data")
        return [h - 16 for h in hits]

    def vtables_of(self, mangled, image_base):
        """Vtable RVAs (one per base sub-object) for a class, from its RTTI."""
        out = []
        for td in self.type_descriptor(mangled):
            for ref in self.find_bytes(struct.pack("<I", td), ".rdata"):
                col = ref - 12
                if self.u32(col) != 1 or self.u32(col + 20) != col:
                    continue
                offset = self.u32(col + 4)
                for p in self.find_qword(image_base + col, ".rdata"):
                    out.append((offset, p + 8))
        return sorted(out)
