#!/usr/bin/env python3
"""Run the built core's OS-independent P-256 machine code at every x86 ABI alignment.

This is an instruction-level regression, not a substitute for booting Haiku.
Only these leaf crypto entry points can be run without the Haiku dynamic loader.
The Linux syscall harness and qemu-i386 let Linux CI execute the actual Haiku code.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("server", type=Path)
parser.add_argument("--legacy-0.1.2", dest="legacy_0_1_2", action="store_true", help="negative control using the released stripped binary")
args = parser.parse_args()
binary = args.server.read_bytes()
assert binary[:6] == b"\x7fELF\x01\x01", "Requires little-endian ELF32"
assert struct.unpack_from("<HH", binary, 16) == (3, 3), "Requires Haiku i386 ET_DYN"
if args.legacy_0_1_2:
    # Recorded from the exact 0.1.2 package, before debug symbols were retained.
    assert hashlib.sha256(binary).hexdigest() == "af025fa7a56f22f1b9543aa9e43b8f237864229cfab08ac8110668c098ebf027"
    symbols = {"p256_mul_mont": 0xff5529, "p256_sqr_mont": 0xff5563}
else:
    symbols = {}
    for line in subprocess.check_output(["nm", "--defined-only", str(args.server)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            for name in ("p256_mul_mont", "p256_sqr_mont"):
                if fields[2].startswith("ring_core_") and fields[2].endswith("__" + name):
                    symbols[name] = int(fields[0], 16)
    assert len(symbols) == 2, "Missing ring symbols; do not strip the core before this test"

# Reconstruct load segments at their virtual offsets; ignore debug information.
phoff = struct.unpack_from("<I", binary, 28)[0]
phsize, phnum = struct.unpack_from("<HH", binary, 42)
segments = [struct.unpack_from("<8I", binary, phoff + i * phsize) for i in range(phnum)]
segments = [p for p in segments if p[0] == 1]
image = bytearray(max(p[2] + p[5] for p in segments))
for _, offset, address, _, filesz, _, _, _ in segments:
    image[address:address + filesz] = binary[offset:offset + filesz]
prime = 2**256 - 2**224 + 2**192 + 2**96 - 1
rinv = pow(2**256, -1, prime)
runner = [shutil.which("qemu-i386")] if shutil.which("qemu-i386") else []
failures = 0
with tempfile.TemporaryDirectory(prefix="parla-x86-stack-") as directory:
    root = Path(directory)
    (root / "image.bin").write_bytes(image)
    for name, offset in symbols.items():
        for alignment in (0, 4, 8, 12):
            # Nontrivial full-width operands also check that realignment preserves
            # arguments and the Montgomery arithmetic, not just absence of a fault.
            a, b = prime - 12345, prime - 67890
            nargs = 3 if name == "p256_mul_mont" else 2
            padding = (-alignment - 4 * nargs) % 16
            push_b = "push $operand_b" if nargs == 3 else ""
            source = f'''.section .text
.global _start
_start:
    and $-16, %esp
    sub ${padding}, %esp
    {push_b}
    push $operand_a
    push $result
    mov $image+{offset}, %eax
    call *%eax
    mov $4, %eax
    mov $1, %ebx
    mov $result, %ecx
    mov $32, %edx
    int $0x80
    mov $1, %eax
    xor %ebx, %ebx
    int $0x80
.balign 4096
image:
.incbin "{root / 'image.bin'}"
.balign 16
operand_a: .byte {','.join(map(str, a.to_bytes(32, 'little')))}
operand_b: .byte {','.join(map(str, b.to_bytes(32, 'little')))}
.section .bss
.balign 16
result: .space 32
.section .note.GNU-stack,"",@progbits
'''
            (root / "harness.s").write_text(source)
            subprocess.run(["as", "--32", str(root / "harness.s"), "-o", str(root / "harness.o")], check=True)
            subprocess.run(["ld", "-m", "elf_i386", str(root / "harness.o"), "-o", str(root / "harness")], check=True)
            run = subprocess.run([*runner, str(root / "harness")], capture_output=True, timeout=10)
            if args.legacy_0_1_2 and run.returncode in (-11, 139) and alignment != 0:
                failures += 1
                print(f"legacy: {name}, stack mod 16 = {alignment}: reproduced SIGSEGV")
                continue
            assert run.returncode == 0, (name, alignment, run.returncode, run.stderr)
            expected = a * (b if nargs == 3 else a) * rinv % prime
            assert run.stdout == expected.to_bytes(32, "little"), (name, alignment, "wrong result")
            print(f"{name}, stack mod 16 = {alignment}: arithmetic OK")
if args.legacy_0_1_2:
    assert failures > 0, "Negative control failed to reproduce the released crash"
