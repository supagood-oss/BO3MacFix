#!/usr/bin/env python3
"""Execute our generated x86 stubs and assembly bridges on synthetic memory.

No game, mod DLL, network or OS initialization is executed. Requires a host C
compiler and the pinned development Unicorn/Capstone dependencies. This is an
instruction/ABI check, not a Rosetta or live-engine qualification.
"""
from pathlib import Path
import argparse
import ctypes as c
import itertools
import json
import os
import struct
import subprocess
import sys
import tempfile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
import unicorn.x86_const as x

ROOT = Path(__file__).resolve().parents[1]
B, CODE, SITE, SLOT, CONSTANT = 0x10000000, 0x10001000, 0x10002000, 0x10003000, 0x10004000
STACK, SP, BP, BRIDGE = 0x20000000, 0x20008000, 0x20008400, 0x30000000
NAMES = ['R15', 'R14', 'R13', 'R12', 'R11', 'R10', 'R9', 'R8', 'RDI', 'RSI', 'RBP', 'RBX', 'RDX', 'RCX', 'RAX']
REGS = [getattr(x, 'UC_X86_REG_' + name) for name in NAMES]
INITIAL = {reg: 0x1122334400000000 + i * 0x101 for i, reg in enumerate(REGS)}
INITIAL[x.UC_X86_REG_RBP] = BP


def vm(code, slot=0, flags=0x202, registers=None):
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    uc.mem_map(B, 0x10000); uc.mem_map(STACK, 0x10000)
    uc.mem_write(CODE, code); uc.mem_write(SLOT, struct.pack('<Q', slot))
    uc.mem_write(CONSTANT, struct.pack('<f', 0.7))
    uc.mem_write(SP - 128, bytes(range(128)))
    for reg, value in (INITIAL | (registers or {})).items(): uc.reg_write(reg, value)
    uc.reg_write(x.UC_X86_REG_RSP, SP); uc.reg_write(x.UC_X86_REG_EFLAGS, flags)
    for i in range(16): uc.reg_write(x.UC_X86_REG_XMM0 + i, (0xabcdef0000000000 + i) << 64 | 0x3f800000)
    return uc


def run(uc, entry, exits):
    exits = set(exits)
    def stop(machine, ip, size, context):
        if ip in exits: machine.emu_stop()
    handle = uc.hook_add(UC_HOOK_CODE, stop)
    uc.emu_start(entry, 0, count=2000, timeout=1000000)
    uc.hook_del(handle)
    assert uc.reg_read(x.UC_X86_REG_RIP) in exits, 'stub escaped its bounded exits'
    return ([uc.reg_read(reg) for reg in REGS], uc.reg_read(x.UC_X86_REG_RSP),
            uc.reg_read(x.UC_X86_REG_EFLAGS) & 0xcd5,
            [uc.reg_read(x.UC_X86_REG_XMM0 + i) for i in range(16)],
            bytes(uc.mem_read(BP - 64, 16)), uc.reg_read(x.UC_X86_REG_RIP))


def text_section(path):
    """Read just .text from a compiler-produced COFF, ELF64 or Mach-O object."""
    data = path.read_bytes()
    if data[:4] == b'\x7fELF':
        table = struct.unpack_from('<Q', data, 40)[0]
        size, count, names = struct.unpack_from('<HHH', data, 58)
        names_header = table + size * names
        offset, length = struct.unpack_from('<QQ', data, names_header + 24)
        strings = data[offset:offset + length]
        for i in range(count):
            header = table + size * i; name = struct.unpack_from('<I', data, header)[0]
            if strings[name:].split(b'\0', 1)[0] == b'.text':
                offset, length = struct.unpack_from('<QQ', data, header + 24)
                return data[offset:offset + length]
    elif data[:4] == b'\xcf\xfa\xed\xfe':
        offset = 32
        for _ in range(struct.unpack_from('<I', data, 16)[0]):
            command, size = struct.unpack_from('<II', data, offset)
            if command == 0x19:
                for i in range(struct.unpack_from('<I', data, offset + 64)[0]):
                    section = offset + 72 + 80 * i
                    if data[section:section + 16].split(b'\0', 1)[0] == b'__text':
                        length, start = struct.unpack_from('<QI', data, section + 40)
                        return data[start:start + length]
            offset += size
    elif data[:2] == b'\x64\x86':
        count = struct.unpack_from('<H', data, 2)[0]
        start = 20 + struct.unpack_from('<H', data, 16)[0]
        for i in range(count):
            section = start + 40 * i
            if data[section:section + 8].split(b'\0', 1)[0] == b'.text':
                length, offset = struct.unpack_from('<II', data, section + 16)
                return data[offset:offset + length]
    raise ValueError('Compiler object has no supported text section')


def check(compiler):
    cases = 0
    with tempfile.TemporaryDirectory(prefix='bo3-stub-check-', ignore_cleanup_errors=True) as directory:
        temporary = Path(directory)
        library = temporary / ('codegen.dll' if os.name == 'nt' else 'codegen.dylib' if sys.platform == 'darwin' else 'codegen.so')
        build = ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared']
        if os.name != 'nt': build += ['-fPIC']
        subprocess.run([compiler, '-O1', *build, str(ROOT / 'source/t7o_codegen.c'), '-o', str(library)], check=True)
        native = c.CDLL(str(library))
        def generate(name, original, *extra):
            output = (c.c_uint8 * 64)(); source = (c.c_uint8 * len(original)).from_buffer_copy(original)
            function = getattr(native, 't7o_codegen_' + name)
            function.argtypes = [c.c_void_p, c.c_size_t, c.c_uint64, c.c_uint64, c.c_uint64, c.c_void_p] + [c.c_size_t] * len(extra)
            function.restype = c.c_size_t
            length = function(output, 64, CODE, SITE, SLOT, source, *extra)
            assert length
            return bytes(output[:length])
        for original, reg in [(b'\x3d\xdc\x05\0\0', x.UC_X86_REG_RAX), (b'\x81\xf9\xdc\x05\0\0', x.UC_X86_REG_RCX)]:
            for enabled, threshold, value in itertools.product((False, True), (1, 9, 500), (0, 1, 9, 10, 499, 500, 1499, 1500, 0x7fffffff, 0x80000000, 0xffffffff)):
                code = generate('cmp32', original, len(original))
                actual = vm(code, (1 << 32 | threshold) if enabled else 0, registers={reg:value})
                expected = vm(b'', registers={reg:value})
                comparison = original[:-4] + struct.pack('<I', threshold) if enabled else original
                expected.mem_write(SITE, comparison)
                assert run(actual, CODE, [SITE + len(original)]) == run(expected, SITE, [SITE + len(original)])
                cases += 1
        flag_values = [0x202 | sum(bit for i, bit in enumerate((1, 4, 16, 64, 128, 2048)) if value >> i & 1) for value in range(64)]
        for original in (b'\x7d\x2e\x0f\x57\xc0', b'\x0f\x84\x4b\x01\0\0'):
            branch = 2 if original[0] != 15 else 6
            displacement = int.from_bytes(original[1:2] if branch == 2 else original[2:6], 'little', signed=True)
            target = SITE + branch + displacement
            for enabled, taken, flags in itertools.product((False, True), (False, True), flag_values):
                code = generate('branch', original, len(original), int(taken))
                actual = vm(code, 1 << 32 if enabled else 0, flags)
                expected = vm(b'', flags=flags)
                replacement = original
                if enabled:
                    replacement = ((b'\xeb' + original[1:2]) if branch == 2 else (b'\xe9' + struct.pack('<i', target-SITE-5) + b'\x90')) if taken else b'\x90' * branch
                    replacement += original[branch:]
                expected.mem_write(SITE, replacement)
                assert run(actual, CODE, [target, SITE+len(original)]) == run(expected, SITE, [target, SITE+len(original)])
                cases += 1
        for operation in (0x10, 0x59, 0x5e):
            original = bytes((0xf3, 0x0f, operation, 5)) + struct.pack('<i', CONSTANT-SITE-8)
            for enabled, value in itertools.product((False, True), (0., 0.4444, 1.4, 2.15, 3.)):
                bits = struct.unpack('<I', struct.pack('<f', value))[0]
                actual = vm(generate('float', original), (1 << 32 | bits) if enabled else 0)
                expected = vm(b''); expected.mem_write(SLOT, struct.pack('<f', value))
                expected.mem_write(SITE, original[:4] + struct.pack('<i', (SLOT if enabled else CONSTANT)-SITE-8))
                assert run(actual, CODE, [SITE+8]) == run(expected, SITE, [SITE+8])
                cases += 1
        original = bytes.fromhex('f30f58c00f2945c0')
        for enabled, value in itertools.product((False, True), (1., 1.4, 2.15, 3.)):
            bits = struct.unpack('<I', struct.pack('<f', value))[0]
            actual = vm(generate('ads_scale', original), (1 << 32 | bits) if enabled else 0)
            expected = vm(b''); expected.mem_write(SLOT, struct.pack('<f', value))
            expected.mem_write(SITE, bytes.fromhex('f30f5905') + struct.pack('<i', SLOT-SITE-8) + original[4:] if enabled else original)
            result = run(actual, CODE, [SITE+8]); reference = run(expected, SITE, [SITE+(12 if enabled else 8)])
            assert result[:-1] == reference[:-1]
            cases += 1
        obj = temporary / 'bridge.o'
        subprocess.run([compiler, '-c', str(ROOT / 'source/t7o_inline.S'), '-o', str(obj)], check=True)
        machine = text_section(obj)
        prefix = bytes.fromhex('9c50515253555657')
        starts = [i for i in range(len(machine)) if machine.startswith(prefix, i)]
        assert len(starts) == 6
        decoder = Cs(CS_ARCH_X86, CS_MODE_64)
        calls = {instruction.address for instruction in decoder.disasm(machine, BRIDGE) if instruction.mnemonic == 'call'}
        for operation, start in enumerate(starts):
            for enabled, flags, mutate, short in itertools.product((False, True), (0x202, 0x6d7, 0xed7), (False, True), (False, True)):
                original = bytes.fromhex('7647f30f51d2' if short else '4889d84889d1')
                actual = vm(generate('inline', original, len(original), int(short)), BRIDGE+start if enabled else 0, flags)
                actual.mem_map(BRIDGE, (len(machine)+4095)&~4095); actual.mem_write(BRIDGE, machine)
                observed = []
                def callback(uc, ip, size, context):
                    if ip not in calls: return
                    pointer = uc.reg_read(x.UC_X86_REG_RDI)
                    assert uc.reg_read(x.UC_X86_REG_RSI) == operation
                    assert pointer+528 == SP and uc.reg_read(x.UC_X86_REG_RSP) % 16 == 0
                    assert not uc.reg_read(x.UC_X86_REG_EFLAGS) & 0x400
                    saved = bytes(uc.mem_read(pointer, 392))
                    assert list(struct.unpack_from('<15Q', saved, 264)) == [INITIAL[r] for r in REGS]
                    assert struct.unpack_from('<Q', saved, 384)[0] & 0xcd5 == flags & 0xcd5
                    if mutate:
                        uc.mem_write(pointer+312, struct.pack('<Q', 77))  # r9
                        uc.mem_write(pointer+240, bytes(range(16)))  # xmm15
                        if short: uc.mem_write(pointer+384, struct.pack('<Q', flags | 1))
                    for reg in REGS: uc.reg_write(reg, 99)
                    for i in range(16): uc.reg_write(x.UC_X86_REG_XMM0+i, 0)
                    uc.reg_write(x.UC_X86_REG_EFLAGS, 0x202)
                    uc.reg_write(x.UC_X86_REG_RIP, ip+size); observed.append(operation)
                actual.hook_add(UC_HOOK_CODE, callback)
                exits = [SITE+len(original)] + ([SITE+2+0x47] if short else [])
                result = run(actual, CODE, exits)
                expected = vm(b'', flags=flags); expected.mem_write(SITE, original)
                if enabled and mutate:
                    expected.reg_write(x.UC_X86_REG_R9, 77)
                    expected.reg_write(x.UC_X86_REG_XMM15, int.from_bytes(bytes(range(16)), 'little'))
                    if short: expected.reg_write(x.UC_X86_REG_EFLAGS, flags | 1)
                assert result == run(expected, SITE, exits)
                assert len(observed) == int(enabled)
                assert bytes(actual.mem_read(SP-128, 128)) == bytes(range(128))
                cases += 1
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(native._handle)
    return {'schema':1, 'cases_passed':cases, 'synthetic_memory_only':True,
            'assembly_bridges_checked':6, 'live_mac_qualified':False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args(); result = check(args.compiler)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps(result))
