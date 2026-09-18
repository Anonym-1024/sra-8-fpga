#!/usr/bin/env python3
"""
sra8asm.py -- assembler for the SRA-8 CPU.

Usage:  sra8asm.py source.s [-o out.mem] [-f mem|bin] [-l listing.lst] [--size N]

Instruction word (32 bit, stored big-endian: frame 0 = bits 31:24 comes first),
taken from InstructionRegister.v:

    [31:28] cond   [26:20] opcode   [19:16] arg1   [15:12] arg2   [11:8] arg3
    [7:0]   imm8 (imm0)             [15:0]  imm16 (imm1:imm0)

Opcode numbers come from control_unit_gen/control_rom_gen.c and follow the
order of the "Instruction set" sheet in Instructions.xlsx.  Bit 0 of the
opcode selects the immediate form, so  opcode = base | 1  when the last
operand is an immediate.

Source syntax
    ; comment
    label:                      global label, value = 16 bit address
    .l loop:                    local label, may be defined many times
    br.ne .b =loop              nearest 'loop' before (.b) / after (.f) this line
    mova r2a, =label            label reference, only where imm16 is allowed
    ldr  r4, r2a                rNa = 16 bit address register, the pair rN (low) : rN+1 (high)
    mov  r1, #0x1f              immediates: #123 #0d123 #0x7b #0o173 #0b1111011 #-1 #'a'
    add.eq r1, r2, r3           optional condition suffix: al eq mi vs su gu ss gs
                                                           nvr ne pl vc geu seu ges ses
    ptr r1, p0 / ptw p0, r1     port operand is optional (the CPU has one port)

Directives
    .code / .data               select section (instructions are rejected in .data)
    .org  #addr                 set the location counter
    .align #n                   advance to a multiple of n
    .word  #1, #2, ...          initialized 8 bit words (.byte is the same)
    .dword #1, ...              initialized 16 bit values, little-endian
    .qword #1, ...              initialized 32 bit values, little-endian
    .addr  =label, #1, ...      initialized 16 bit addresses, little-endian (as page table entries)
    .ascii "text" / .asciz "text"
    .res  #n                    n uninitialized bytes

Preprocessor (pure text, runs before assembly)
    !INCLUDE file               paste file (path relative to the including file)
    !DEFINE alias text          define alias
    !alias                      replaced by <text>
"""

import argparse
import os
import re
import sys

# --------------------------------------------------------------------------
# Instruction encoding
# --------------------------------------------------------------------------

COND_SHIFT = 28
OPCODE_SHIFT = 20
ARG1_SHIFT = 16
ARG2_SHIFT = 12
ARG3_SHIFT = 8

INSTR_SIZE = 4
PAGE_SIZE = 256
ADDR_SPACE = 1 << 16

CONDITIONS = {
    "al": 0, "eq": 1, "mi": 2, "vs": 3, "su": 4, "gu": 5, "ss": 6, "gs": 7,
    "nvr": 8, "ne": 9, "pl": 10, "vc": 11, "geu": 12, "seu": 13, "ges": 14, "ses": 15,
}

# Operand formats.  "src" is a register or an immediate; an immediate sets
# opcode bit 0.  Registers go to arg1, arg2, arg3 in operand order.
# rN is an 8 bit register, rNa the 16 bit address register rN (low) : rN+1 (high).
F_NONE = "none"             #
F_RD = "rd"                 # rD
F_RDA = "rda"               # rDa
F_SRC8 = "src8"             # rS / imm8
F_SRC16 = "src16"           # rSa / imm16
F_RD_SRC8 = "rd_src8"       # rD, rS / imm8
F_RD_SRC16 = "rd_src16"     # rD, rSa / imm16
F_RDA_SRC16 = "rda_src16"   # rDa, rSa / imm16
F_ALU3 = "alu3"             # rD, rN, rM / imm8
F_PTR = "ptr"               # rD [, pS]
F_PTW = "ptw"               # [pD,] rS

# format -> (widths of the register operands before src, src width or 0)
FORMATS = {
    F_NONE: ((), 0),
    F_RD: ((8,), 0),
    F_RDA: ((16,), 0),
    F_SRC8: ((), 8),
    F_SRC16: ((), 16),
    F_RD_SRC8: ((8,), 8),
    F_RD_SRC16: ((8,), 16),
    F_RDA_SRC16: ((16,), 16),
    F_ALU3: ((8, 8), 8),
}

INSTRUCTIONS = {
    # register operations
    "mov": (0, F_RD_SRC8),
    "mova": (2, F_RDA_SRC16),
    "pcw": (4, F_SRC16),
    "pcr": (6, F_RDA),
    "xpcw": (8, F_SRC16),       # current program counter: PC, or INTPC while interrupted
    "xpcr": (10, F_RDA),
    "intpcw": (12, F_SRC16),
    "intpcr": (14, F_RDA),
    "psrw": (16, F_SRC8),
    "psrr": (18, F_RD),
    "ptbrw": (20, F_SRC16),
    "ptbrr": (22, F_RDA),
    "intrw": (24, F_SRC8),
    "intrr": (26, F_RD),
    # memory access
    "ldr": (28, F_RD_SRC16),
    "str": (30, F_RD_SRC16),
    # arithmetic and logic
    "add": (32, F_ALU3), "adds": (34, F_ALU3),
    "addc": (36, F_ALU3), "addcs": (38, F_ALU3),
    "sub": (40, F_ALU3), "subs": (42, F_ALU3),
    "subc": (44, F_ALU3), "subcs": (46, F_ALU3),
    "and": (48, F_ALU3), "ands": (50, F_ALU3),
    "or": (52, F_ALU3), "ors": (54, F_ALU3),
    "eor": (56, F_ALU3), "eors": (58, F_ALU3),
    # shifts
    "lsl": (60, F_RD_SRC8), "lsls": (62, F_RD_SRC8),
    "lsr": (64, F_RD_SRC8), "lsrs": (66, F_RD_SRC8),
    "asr": (68, F_RD_SRC8), "asrs": (70, F_RD_SRC8),
    "csl": (72, F_RD_SRC8), "csls": (74, F_RD_SRC8),
    "csr": (76, F_RD_SRC8), "csrs": (78, F_RD_SRC8),
    # flags only
    "cmn": (80, F_RD_SRC8), "addcd": (82, F_RD_SRC8),
    "cmp": (84, F_RD_SRC8), "subcd": (86, F_RD_SRC8),
    "andd": (88, F_RD_SRC8), "ord": (90, F_RD_SRC8), "eord": (92, F_RD_SRC8),
    # shifts, flags only
    "lsld": (94, F_SRC8), "lsrd": (96, F_SRC8), "asrd": (98, F_SRC8),
    "csld": (100, F_SRC8), "csrd": (102, F_SRC8),
    # branching
    "br": (104, F_SRC16),
    "brl": (106, F_RDA_SRC16),
    # port I/O
    "ptr": (108, F_PTR),
    "ptw": (110, F_PTW),
    # other
    "svc": (112, F_NONE),
}

# Data directives -> size of one value in bytes.  A word is 8 bits on this CPU.
DATA_WIDTHS = {".byte": 1, ".word": 1, ".dword": 2, ".qword": 4, ".addr": 2}

# In Instructions.xlsx but without microcode in control_rom_gen.c
UNIMPLEMENTED = ("movs", "mvn", "mvns", "ptsr")


class AsmError(Exception):
    pass


# --------------------------------------------------------------------------
# Preprocessor: !INCLUDE, !DEFINE, !alias.  Produces (file, line_no, text).
# --------------------------------------------------------------------------

MAX_INCLUDE_DEPTH = 32
MAX_EXPAND_DEPTH = 32

RE_ALIAS = re.compile(r"!([A-Za-z_][A-Za-z0-9_]*)")


def strip_comment(text):
    quote = None
    escaped = False
    for i, ch in enumerate(text):
        if escaped:
            escaped = False
        elif quote:
            if ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == ";":
            return text[:i]
    return text


def expand_aliases(text, defines, where):
    """Replace every !alias outside of quotes, repeatedly, so aliases may nest."""
    for _ in range(MAX_EXPAND_DEPTH):
        out = ""
        quote = None
        changed = False
        i = 0
        while i < len(text):
            ch = text[i]
            m = RE_ALIAS.match(text, i) if ch == "!" and not quote else None
            if m:
                if m.group(1) not in defines:
                    raise AsmError("%s: undefined macro '!%s'" % (where, m.group(1)))
                out += defines[m.group(1)]
                changed = True
                i = m.end()
                continue
            if quote:
                if ch == "\\" and i + 1 < len(text):
                    out += ch
                    i += 1
                    ch = text[i]
                elif ch == quote:
                    quote = None
            elif ch in "\"'":
                quote = ch
            out += ch
            i += 1
        if not changed:
            return out
        text = out
    raise AsmError("%s: macro expansion too deep (recursive !DEFINE?)" % where)


def preprocess(path, defines, out, depth=0):
    if depth > MAX_INCLUDE_DEPTH:
        raise AsmError("%s: !INCLUDE nested too deep" % path)
    try:
        with open(path, "r") as f:
            raw_lines = f.read().split("\n")
    except OSError as e:
        raise AsmError("cannot read '%s': %s" % (path, e.strerror))

    for line_no, raw in enumerate(raw_lines, 1):
        where = "%s:%d" % (path, line_no)
        text = strip_comment(raw).strip()
        if not text:
            continue
        words = text.split(None, 2)
        if words[0] == "!INCLUDE":
            # the rest of the line is the path, so it may contain spaces
            name = expand_aliases(text[len("!INCLUDE"):].strip(), defines, where)
            if not name:
                raise AsmError("%s: expected  !INCLUDE file" % where)
            preprocess(os.path.join(os.path.dirname(path), name), defines, out, depth + 1)
        elif words[0] == "!DEFINE":
            if len(words) < 2 or not RE_ALIAS.fullmatch("!" + words[1]):
                raise AsmError("%s: expected  !DEFINE <alias> <replacement>" % where)
            defines[words[1]] = words[2] if len(words) == 3 else ""
        else:
            out.append((path, line_no, expand_aliases(text, defines, where)))


# --------------------------------------------------------------------------
# Operand parsing
# --------------------------------------------------------------------------

RE_LABEL_DEF = re.compile(r"(\.l\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*")
RE_REGISTER = re.compile(r"r([0-9]|1[0-5])(a?)")
RE_PORT = re.compile(r"p([0-9]|1[0-5])")
RE_LABEL_REF = re.compile(r"(?:\.([bf])\s+)?=([A-Za-z_][A-Za-z0-9_]*)")

RADIX = {"0b": 2, "0o": 8, "0d": 10, "0x": 16}
ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", "'": "'", '"': '"'}


def unescape(s):
    out = ""
    i = 0
    while i < len(s):
        if s[i] == "\\":
            i += 1
            if i >= len(s) or s[i] not in ESCAPES:
                raise AsmError("bad escape sequence in '%s'" % s)
            out += ESCAPES[s[i]]
        else:
            out += s[i]
        i += 1
    return out


def parse_number(text):
    """'123', '-0x1f', "'a'" -> int"""
    if len(text) >= 3 and text[0] == "'" and text[-1] == "'":
        ch = unescape(text[1:-1])
        if len(ch) != 1:
            raise AsmError("bad character constant %s" % text)
        return ord(ch)
    body = text
    sign = 1
    if body[:1] in ("-", "+"):
        sign = -1 if body[0] == "-" else 1
        body = body[1:]
    base = RADIX.get(body[:2], 10)
    if body[:2] in RADIX:
        body = body[2:]
    try:
        if not body or not body[0].isalnum():
            raise ValueError
        return sign * int(body, base)
    except ValueError:
        raise AsmError("bad number '%s'" % text)


def parse_operand(text):
    """-> ('reg', n) | ('areg', n) | ('port', n) | ('imm', value) | ('label', name, direction)"""
    m = RE_REGISTER.fullmatch(text)
    if m:
        return ("areg" if m.group(2) else "reg", int(m.group(1)))
    m = RE_PORT.fullmatch(text)
    if m:
        return ("port", int(m.group(1)))
    if text.startswith("#"):
        return ("imm", parse_number(text[1:].strip()))
    m = RE_LABEL_REF.fullmatch(text)
    if m:
        return ("label", m.group(2), m.group(1))
    raise AsmError("bad operand '%s'" % text)


def parse_value(text):
    """Directive argument: '#' is optional for numbers."""
    if text[:1] in "=.":
        return parse_operand(text)
    return ("imm", parse_number(text[1:].strip() if text.startswith("#") else text))


def split_operands(text):
    parts = []
    cur = ""
    quote = None
    escaped = False
    for ch in text:
        if escaped:
            cur += ch
            escaped = False
        elif quote:
            cur += ch
            if ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
            cur += ch
        elif ch == ",":
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if quote:
        raise AsmError("unterminated string")
    if cur.strip() or parts:
        parts.append(cur.strip())
    if "" in parts:
        raise AsmError("empty operand")
    return parts


def fit(value, bits):
    if value < -(1 << (bits - 1)) or value >= (1 << bits):
        raise AsmError("value %d does not fit in %d bits" % (value, bits))
    return value & ((1 << bits) - 1)


# --------------------------------------------------------------------------
# Assembler
# --------------------------------------------------------------------------

class Item:
    """One thing that occupies memory.  kind: 'instr' | 'data' | 'res'."""
    def __init__(self, kind, where, addr, size, source):
        self.kind = kind
        self.where = where
        self.addr = addr
        self.size = size
        self.source = source
        self.mnemonic = None
        self.cond = 0
        self.operands = []     # instr: parsed operands; data: (width, operand) list
        self.bytes = b""


class Assembler:
    def __init__(self):
        self.items = []
        self.labels = {}        # name -> addr
        self.local_labels = {}  # name -> [(item index, addr), ...] in source order
        self.warnings = []

    # ---- pass 1: parse, assign addresses, collect labels -----------------

    def pass1(self, lines):
        pc = 0
        section = "code"
        for path, line_no, text in lines:
            where = "%s:%d" % (path, line_no)
            try:
                while True:
                    m = RE_LABEL_DEF.match(text)
                    if not m:
                        break
                    name = m.group(2)
                    if m.group(1):
                        self.local_labels.setdefault(name, []).append((len(self.items), pc))
                    elif name in self.labels:
                        raise AsmError("label '%s' already defined" % name)
                    else:
                        self.labels[name] = pc
                    text = text[m.end():]
                if not text:
                    continue

                word, rest = (text.split(None, 1) + [""])[:2]
                ops = split_operands(rest.strip())

                if word in (".code", ".data"):
                    self.expect_count(word, ops, 0)
                    section = word[1:]
                    continue
                if word == ".org":
                    self.expect_count(word, ops, 1)
                    pc = self.constant(ops[0])
                    continue
                if word == ".align":
                    self.expect_count(word, ops, 1)
                    n = self.constant(ops[0])
                    if n < 1:
                        raise AsmError(".align needs a positive value")
                    pad = -pc % n
                    if pad:
                        self.items.append(Item("res", where, pc, pad, text))
                    pc += pad
                    continue

                if word.startswith("."):
                    item = self.parse_data(word, ops, where, pc, text)
                else:
                    if section != "code":
                        raise AsmError("instruction in .data section")
                    item = self.parse_instr(word, ops, where, pc, text)
                if pc + item.size > ADDR_SPACE:
                    raise AsmError("address 0x%X is outside the 16 bit address space" % (pc + item.size - 1))
                self.items.append(item)
                pc += item.size
            except AsmError as e:
                raise AsmError("%s: %s" % (where, e))

    def expect_count(self, word, ops, n):
        if len(ops) != n:
            raise AsmError("'%s' takes %d operand%s, got %d" % (word, n, "" if n == 1 else "s", len(ops)))

    def constant(self, text):
        op = parse_value(text)
        if op[0] != "imm":
            raise AsmError("expected a number, got '%s'" % text)
        if not 0 <= op[1] < ADDR_SPACE:
            raise AsmError("value %d out of range" % op[1])
        return op[1]

    def parse_data(self, word, ops, where, pc, text):
        if word == ".res":
            self.expect_count(word, ops, 1)
            return Item("res", where, pc, self.constant(ops[0]), text)
        item = Item("data", where, pc, 0, text)
        if word in DATA_WIDTHS:
            width = DATA_WIDTHS[word]
            if not ops:
                raise AsmError("'%s' needs at least one value" % word)
            for t in ops:
                op = parse_value(t)
                if op[0] == "label" and word != ".addr":
                    raise AsmError("label reference not allowed in '%s', use .addr" % word)
                if op[0] not in ("imm", "label"):
                    raise AsmError("bad value '%s'" % t)
                item.operands.append((width, op))
            item.size = width * len(ops)
        elif word in (".ascii", ".asciz"):
            self.expect_count(word, ops, 1)
            s = ops[0]
            if len(s) < 2 or s[0] != '"' or s[-1] != '"':
                raise AsmError("'%s' needs a \"string\"" % word)
            s = unescape(s[1:-1]) + ("\0" if word == ".asciz" else "")
            try:
                item.bytes = s.encode("latin-1")
            except UnicodeEncodeError:
                raise AsmError("string contains characters outside 8 bit range")
            item.size = len(item.bytes)
        else:
            raise AsmError("unknown directive '%s'" % word)
        return item

    def parse_instr(self, word, ops, where, pc, text):
        mnemonic, dot, cond = word.partition(".")
        if mnemonic in UNIMPLEMENTED:
            raise AsmError("'%s' has no microcode in the control ROM" % mnemonic)
        if mnemonic not in INSTRUCTIONS:
            raise AsmError("unknown instruction '%s'" % mnemonic)
        if dot and cond not in CONDITIONS:
            raise AsmError("unknown condition '%s'" % cond)
        item = Item("instr", where, pc, INSTR_SIZE, text)
        if pc % PAGE_SIZE > PAGE_SIZE - INSTR_SIZE:
            self.warnings.append("%s: instruction at 0x%04X crosses a page boundary, fetch cannot follow it" % (where, pc))
        item.mnemonic = mnemonic
        item.cond = CONDITIONS[cond] if dot else 0
        item.operands = [parse_operand(t) for t in ops]
        return item

    # ---- pass 2: resolve labels, encode ----------------------------------

    def resolve(self, op, index):
        _, name, direction = op
        if direction is None:
            if name not in self.labels:
                hint = " (local label? use .b / .f)" if name in self.local_labels else ""
                raise AsmError("undefined label '%s'%s" % (name, hint))
            return self.labels[name]
        defs = self.local_labels.get(name, [])
        if direction == "b":
            found = [addr for i, addr in defs if i <= index]
            if found:
                return found[-1]
        else:
            found = [addr for i, addr in defs if i > index]
            if found:
                return found[0]
        raise AsmError("no local label '%s' %s this line" % (name, "before" if direction == "b" else "after"))

    def encode_instr(self, item, index):
        opcode, fmt = INSTRUCTIONS[item.mnemonic]
        ops = list(item.operands)

        # the port number is not decoded by the hardware; it is kept in arg2
        port = 0
        if fmt == F_PTR:
            if len(ops) == 2 and ops[1][0] == "port":
                port = ops.pop()[1]
            fmt = F_RD
        elif fmt == F_PTW:
            if len(ops) == 2 and ops[0][0] == "port":
                port = ops.pop(0)[1]
            fmt = F_RD

        reg_bits, src_bits = FORMATS[fmt]
        n_ops = len(reg_bits) + (1 if src_bits else 0)
        if len(ops) != n_ops:
            raise AsmError("'%s' takes %d operand%s, got %d" % (item.mnemonic, n_ops, "" if n_ops == 1 else "s", len(ops)))

        word = (item.cond << COND_SHIFT) | (port << ARG2_SHIFT)
        shifts = [ARG1_SHIFT, ARG2_SHIFT, ARG3_SHIFT]
        for i, op in enumerate(ops):
            is_src = src_bits and i == n_ops - 1
            bits = src_bits if is_src else reg_bits[i]
            if op[0] in ("reg", "areg"):
                if (op[0] == "areg") != (bits == 16):
                    raise AsmError("operand %d of '%s' must be %s, got 'r%d%s'" % (
                        i + 1, item.mnemonic,
                        "a 16 bit address register (r%da)" % op[1] if bits == 16 else "an 8 bit register (r%d)" % op[1],
                        op[1], "a" if op[0] == "areg" else ""))
                if bits == 16 and op[1] == 15:
                    self.warnings.append("%s: r15a has no high register, the high byte wraps to r0" % item.where)
                word |= op[1] << shifts[i]
            elif not is_src:
                raise AsmError("operand %d of '%s' must be a register" % (i + 1, item.mnemonic))
            elif op[0] == "imm":
                opcode |= 1
                word |= fit(op[1], src_bits)
            elif op[0] == "label":
                if src_bits != 16:
                    raise AsmError("'%s' takes an 8 bit immediate, label reference not allowed" % item.mnemonic)
                opcode |= 1
                word |= self.resolve(op, index)
            else:
                raise AsmError("operand %d of '%s' must be a register or an immediate" % (i + 1, item.mnemonic))

        word |= opcode << OPCODE_SHIFT
        return word.to_bytes(INSTR_SIZE, "big")

    def pass2(self):
        for index, item in enumerate(self.items):
            try:
                if item.kind == "instr":
                    item.bytes = self.encode_instr(item, index)
                elif item.kind == "data" and item.operands:
                    out = b""
                    for width, op in item.operands:
                        value = self.resolve(op, index) if op[0] == "label" else fit(op[1], 8 * width)
                        out += value.to_bytes(width, "little")
                    item.bytes = out
            except AsmError as e:
                raise AsmError("%s: %s" % (item.where, e))

    # ---- output -----------------------------------------------------------

    def image(self, size):
        """-> (bytearray image, highest used address + 1)"""
        mem = bytearray(size)
        owner = {}
        end = 0
        for item in self.items:
            if item.size == 0:
                continue
            if item.addr + item.size > size:
                raise AsmError("%s: address 0x%04X is outside the %d byte memory (see --size)"
                               % (item.where, item.addr + item.size - 1, size))
            for a in range(item.addr, item.addr + item.size):
                if a in owner:
                    raise AsmError("%s: overlaps %s at address 0x%04X" % (item.where, owner[a], a))
                owner[a] = item.where
            if item.kind != "res":
                mem[item.addr:item.addr + item.size] = item.bytes
            end = max(end, item.addr + item.size)
        return mem, end

    def listing(self):
        lines = []
        for item in self.items:
            data = item.bytes if item.kind != "res" else b""
            head = " ".join("%02X" % b for b in data[:8])
            if item.kind == "res":
                head = "(%d bytes)" % item.size
            lines.append("%04X  %-24s %s" % (item.addr, head, item.source))
            for i in range(8, len(data), 8):
                lines.append("%04X  %s" % (item.addr + i, " ".join("%02X" % b for b in data[i:i + 8])))
        lines.append("")
        for name in sorted(self.labels, key=lambda n: self.labels[n]):
            lines.append("%04X  %s" % (self.labels[name], name))
        return "\n".join(lines) + "\n"


def write_mem(path, mem):
    with open(path, "w") as f:
        f.write("@0000\n")
        for i in range(0, len(mem), 4):
            f.write(" ".join("%02X" % b for b in mem[i:i + 4]) + "\n")


def main():
    ap = argparse.ArgumentParser(description="SRA-8 assembler")
    ap.add_argument("source")
    ap.add_argument("-o", "--output", help="output file (default: source name + .mem / .bin)")
    ap.add_argument("-f", "--format", choices=("mem", "bin"), default="mem",
                    help="mem: $readmemh image of the whole memory (default); bin: raw bytes up to the last used address")
    ap.add_argument("-l", "--listing", help="write a listing file")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=4096,
                    help="memory size in bytes (default 4096, as Memory.v)")
    args = ap.parse_args()

    try:
        lines = []
        preprocess(args.source, {}, lines)
        asm = Assembler()
        asm.pass1(lines)
        asm.pass2()
        mem, end = asm.image(args.size)
    except AsmError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1

    for w in asm.warnings:
        print("warning: %s" % w, file=sys.stderr)

    out = args.output or os.path.splitext(args.source)[0] + "." + args.format
    if args.format == "mem":
        write_mem(out, mem)
    else:
        with open(out, "wb") as f:
            f.write(mem[:end])
    if args.listing:
        with open(args.listing, "w") as f:
            f.write(asm.listing())
    return 0


if __name__ == "__main__":
    sys.exit(main())
