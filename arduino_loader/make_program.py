#!/usr/bin/env python3
"""
make_program.py -- assemble an SRA-8 program and turn it into program.h,
the byte array that arduino_loader.ino uploads to loader.s.

Usage:  python3 arduino_loader/make_program.py terminal.s
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ASSEMBLER = os.path.join(HERE, "..", "asm", "sra8asm.py")
MAX_SIZE = 12 * 256             # physical pages 4..15, see loader.s


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 1
    source = sys.argv[1]

    with tempfile.TemporaryDirectory() as tmp:
        binary = os.path.join(tmp, "program.bin")
        result = subprocess.run([sys.executable, ASSEMBLER, source, "-f", "bin", "-o", binary])
        if result.returncode != 0:
            return result.returncode
        with open(binary, "rb") as f:
            data = f.read()

    if len(data) > MAX_SIZE:
        print("error: %d bytes, the loader has room for %d" % (len(data), MAX_SIZE), file=sys.stderr)
        return 1

    lines = ["// %s, %d bytes -- made by make_program.py, do not edit" % (os.path.basename(source), len(data)),
             "",
             "const char PROGRAM_NAME[] = \"%s\";" % os.path.basename(source),
             "",
             "const byte program[] PROGMEM = {"]
    for i in range(0, len(data), 12):
        lines.append("  " + ", ".join("0x%02X" % b for b in data[i:i + 12]) + ",")
    lines.append("};")

    out = os.path.join(HERE, "program.h")
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("%s: %d bytes -> %s" % (source, len(data), os.path.relpath(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
