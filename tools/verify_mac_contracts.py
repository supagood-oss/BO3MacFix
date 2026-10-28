#!/usr/bin/env python3
"""Bounded offline ABI checks against the licensed, exact Mac executable.

Runs selected VM, cache-index and weapon-animation routines inside isolated
Unicorn memory. No original game initialization or OS
import is run. The only substituted import is ASCII tolower. This is not a game,
hook, Rosetta or network test. Original executable bytes are never exported.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
from inspect_mac_build import inspect

EXPECTED_SHA256 = "932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0"
BASE = 0x100000000
FUNCTIONS = {"get_int": (0x11a1e78, 0x11a1f26), "push_int": (0x11a317d, 0x11a31b7),
    "allocate_result": (0x11a3120, 0x11a317d), "fx_index": (0x12a7048, 0x12a716e),
    "get_string": (0x11a2890, 0x11a28a1), "get_const_string": (0x119f199, 0x119f219),
    "cast_string": (0x12f74f8, 0x12f75d4), "string_pointer": (0x11b6940, 0x11b6960),
    "weapon_anim_hand": (0xed3b3c, 0xed3b76),
    "strafe_scale": (0x1073e5d, 0x1073e6b),
    "session_mode": (0x127af60, 0x127af77),
    "demo_playing": (0x12c1ab9, 0x12c1ac9),
    "pending_count": (0xff6504, 0xff650e),
    "pending_value": (0x102b646, 0x102b652)}
PUB = BASE + 0x14450ef0
PUB_STRIDE = 0xa048
HEAP = 0x200000000
STACK = 0x300000000
STOP = STACK + 0x100
TOL = BASE + 0x16272d8


def run(data: bytes) -> dict:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
    import unicorn.x86_const as x
    metadata = inspect(data)
    if metadata["sha256"] != EXPECTED_SHA256:
        raise ValueError("Unknown Mac executable; no instructions were emulated")
    vm = Uc(UC_ARCH_X86, UC_MODE_64)
    # __TEXT has the same file/virtual offsets in this qualified thin build.
    vm.mem_map(BASE, (len(data) + 4095) & ~4095)
    vm.mem_write(BASE, data)
    vm.mem_map(HEAP, 0x20000)
    vm.mem_map(STACK, 0x20000)
    for instance in (0, 1):
        address = PUB + instance * PUB_STRIDE
        page = address & ~4095
        vm.mem_map(page, 0x2000)
    string_global = BASE + 0x150e6048
    vm.mem_map(string_global & ~4095, 4096)
    for address in (BASE + 0x6939a14, BASE + 0x1536e038, BASE + 0x40a9410):
        vm.mem_map(address & ~4095, 4096)
    trap = []
    allowed = []

    def hook(uc, address, size, context):
        if address == STOP:
            uc.emu_stop()
        elif address == TOL:
            value = uc.reg_read(x.UC_X86_REG_RDI) & 0xff
            uc.reg_write(x.UC_X86_REG_RAX, value + 32 if 65 <= value <= 90 else value)
            sp = uc.reg_read(x.UC_X86_REG_RSP)
            uc.reg_write(x.UC_X86_REG_RIP, struct.unpack("<Q", uc.mem_read(sp, 8))[0])
            uc.reg_write(x.UC_X86_REG_RSP, sp + 8)
        elif not any(start <= address < end for start, end in allowed):
            trap.append(address)
            uc.emu_stop()
    vm.hook_add(UC_HOOK_CODE, hook)

    def put(address, fmt, *values):
        vm.mem_write(address, struct.pack(fmt, *values))

    def call(name, *arguments, extra=(), pointer_result=False):
        trap.clear()
        allowed[:] = [(BASE + FUNCTIONS[n][0], BASE + FUNCTIONS[n][1]) for n in (name, *extra)]
        for register in (x.UC_X86_REG_RAX, x.UC_X86_REG_RBX, x.UC_X86_REG_RCX,
                         x.UC_X86_REG_RDX, x.UC_X86_REG_RSI, x.UC_X86_REG_RDI,
                         x.UC_X86_REG_R8, x.UC_X86_REG_R9, x.UC_X86_REG_R10,
                         x.UC_X86_REG_R11, x.UC_X86_REG_R12, x.UC_X86_REG_R13,
                         x.UC_X86_REG_R14, x.UC_X86_REG_R15, x.UC_X86_REG_RBP):
            vm.reg_write(register, 0)
        for register, value in zip((x.UC_X86_REG_RDI, x.UC_X86_REG_RSI, x.UC_X86_REG_RDX,
                                   x.UC_X86_REG_RCX, x.UC_X86_REG_R8, x.UC_X86_REG_R9), arguments):
            vm.reg_write(register, value & 0xffffffffffffffff)
        sp = STACK + 0x18000 - 8
        put(sp, "<Q", STOP)
        vm.reg_write(x.UC_X86_REG_RSP, sp)
        vm.emu_start(BASE + FUNCTIONS[name][0], STOP, timeout=1000000, count=200000)
        if not trap and vm.reg_read(x.UC_X86_REG_RIP) != STOP:
            raise AssertionError("Bounded emulation did not return")
        return vm.reg_read(x.UC_X86_REG_RAX) & (0xffffffffffffffff if pointer_result else 0xffffffff)

    cases = []
    for word in (0, 1, 2, 3, 7, 8, 15, 0xa3):
        put(BASE + 0x6939a14, "<I", word)
        signed_mode = (word & 15) - (16 if word & 8 else 0)
        for candidate in (0, 2, 3, 7, -1, -8):
            assert call("session_mode", candidate) & 255 == int(signed_mode == candidate) and not trap
            cases.append(f"session_mode/{word:02x}/{candidate}")
    for state in (0, 1, 2, 3):
        put(BASE + 0x1536e038, "<I", state)
        assert call("demo_playing") & 255 == int(state == 2) and not trap
        cases.append(f"demo_playing/{state}")
    for count in (-1, 0, 1, 42):
        put(BASE + 0x40a9410, "<i", count)
        assert call("pending_count", extra=("pending_value",)) == count & 0xffffffff and not trap
        cases.append(f"pending_count/{count}")
    call("strafe_scale")
    assert vm.reg_read(x.UC_X86_REG_XMM0) & 0xffffffff == 0x3f2ac083 and not trap
    cases.append("strafe_scale/original_baseline")
    # Execute both real prologues at a new address after relocating their
    # explicitly identified RIP-relative operand, as the prepatch builder must.
    trampoline = BASE + 0x2200000
    for label, rva, length, displacement, target in (
        ("client_pause", 0x136b72b, 11, 7, BASE + 0x113b7880),
        ("strafe_scale", 0x1073e5d, 12, 8, BASE + 0x189f1a4),
    ):
        code = bytearray(data[rva:rva + length])
        old = struct.unpack_from("<i", code, displacement)[0]
        assert BASE + rva + length + old == target
        struct.pack_into("<i", code, displacement, target - (trampoline + length))
        vm.mem_write(trampoline, bytes(code))
        allowed[:] = [(trampoline, trampoline + length)]
        trap.clear()
        sp = STACK + 0x18000 - 8
        vm.reg_write(x.UC_X86_REG_RSP, sp)
        vm.reg_write(x.UC_X86_REG_RBP, 0x1234)
        vm.emu_start(trampoline, trampoline + length, timeout=1000000, count=8)
        assert not trap and vm.reg_read(x.UC_X86_REG_RIP) == trampoline + length
        assert vm.reg_read(x.UC_X86_REG_RSP) == sp - 8 and vm.reg_read(x.UC_X86_REG_RBP) == sp - 8
        if label == "client_pause":
            assert vm.reg_read(x.UC_X86_REG_RCX) == target
        else:
            assert vm.reg_read(x.UC_X86_REG_XMM0) & 0xffffffff == 0x3f2ac083
        cases.append(f"trampoline_prefix/{label}/relocated_target")
    for instance in (0, 1):
        pub = PUB + instance * PUB_STRIDE
        top = HEAP + 0x1000 + instance * 0x1000
        put(pub + 0x20, "<Q", top)
        put(pub + 0x38, "<I", 2)
        for tag in (5, 7):  # Hash and integer are both accepted by Scr_GetInt.
            for value in (0, 1, 0x7fffffff, 0x80000000, 0xffffffff):
                put(top - 16, "<QII", value, tag, 0)
                result = call("get_int", instance, 1)
                assert not trap and result == value, (instance, tag, value, result)
                cases.append(f"get_int/instance{instance}/tag{tag}/{value:08x}")
        # Wrong types and missing arguments must enter the engine's error path.
        put(top - 16, "<QII", 1, 6, 0)
        call("get_int", instance, 1)
        assert trap == [BASE + 0x1517339]
        call("get_int", instance, 2)
        assert trap == [BASE + 0x1517339]
        cases.extend([f"get_int/instance{instance}/wrong_type_rejected", f"get_int/instance{instance}/missing_rejected"])
        put(pub + 8, "<Q", top + 0x200)
        put(pub + 0x34, "<II", 0, 0)
        call("push_int", instance, 0xe2a60154, extra=("allocate_result",))
        assert not trap
        assert struct.unpack("<Q", vm.mem_read(pub + 0x20, 8))[0] == top + 16
        assert struct.unpack("<QII", vm.mem_read(top + 16, 16))[:2] == (0xe2a60154, 7)
        assert struct.unpack("<I", vm.mem_read(pub + 0x34, 4))[0] == 1
        cases.append(f"push_int/instance{instance}/signed_high_bit")

        strings = HEAP + 0x1c000
        handle = 3
        text = b"MiXeD_script_name\0"
        put(string_global, "<Q", strings)
        vm.mem_write(strings + handle * 28 + 4, text)
        put(pub + 0x20, "<Q", top)
        put(pub + 0x38, "<I", 2)
        for tag in (2,):
            put(top - 16, "<QII", handle, tag, 0)
            result = call("get_string", instance, 1, pointer_result=True,
                          extra=("get_const_string", "cast_string", "string_pointer"))
            assert not trap and result == strings + handle * 28 + 4, (instance, tag, hex(result), [hex(a) for a in trap])
            assert bytes(vm.mem_read(result, len(text))) == text
            cases.append(f"get_string/instance{instance}/tag{tag}/preserves_case")
        put(top - 16, "<QII", handle, 3, 0)
        call("get_string", instance, 1, extra=("get_const_string", "cast_string", "string_pointer"))
        assert trap == [BASE + 0x1517339]
        cases.append(f"get_string/instance{instance}/localized_string_rejected")
        call("get_string", instance, 2, extra=("get_const_string", "cast_string", "string_pointer"))
        assert trap == [BASE + 0x1517339]
        cases.append(f"get_string/instance{instance}/missing_rejected")

    # Execute the complete per-hand routine. Other animation fields must retain
    # their bytes; this tests the real stride, mask, toggle and PM-type guard.
    ps = HEAP + 0x1d000
    for hand in (0, 1):
        for pm_type in (0, 9, 10):
            for animation in (0, 7, 0x1fff, 0x2001, 0xffffffff):
                before = bytearray(b"\xa5" * 512)
                struct.pack_into("<I", before, 8, pm_type)
                offset = 0x64 + hand * 0x1c
                old = struct.unpack_from("<H", before, offset)[0]
                expected = before.copy()
                if pm_type <= 9:
                    struct.pack_into("<H", expected, offset,
                                     (old & 0xe000 | animation & 0x1fff) ^ 0x2000)
                vm.mem_write(ps, bytes(before))
                call("weapon_anim_hand", ps, animation, hand)
                assert not trap and bytes(vm.mem_read(ps, 512)) == bytes(expected)
                cases.append(f"weapon_anim/hand{hand}/pm{pm_type}/{animation:08x}")

    def cache_hash(text):
        value = 0x1505
        for byte in text.lower().encode("ascii"):
            value = (value * 33 + byte) & 0xffffffff
        return value
    table, name = HEAP + 0x4000, HEAP + 0x18000
    for capacity in (1024, 2048):
        vm.mem_write(table, bytes(capacity * 16))
        vm.mem_write(name, b"Effect_A\0")
        assert call("fx_index", 0, 27, table, capacity, name, 0) == 0 and not trap
        assert call("fx_index", 0, 27, table, capacity, name, 1) == 1 and not trap
        put(table + 16, "<QI4B", 0x1234, cache_hash("effect_a"), 1, 0, 0, 0)
        assert call("fx_index", 1, 27, table, capacity, name, 0) == 1 and not trap
        vm.mem_write(name, b"Effect_B\0")
        assert call("fx_index", 0, 27, table, capacity, name, 1) == 2 and not trap
        # A full prefix must not hide the last supported entry.
        for index in range(1, capacity):
            put(table + 16 * index, "<QI4B", 0x1234, cache_hash(f"effect_{index}"), 1, 0, 0, 0)
        vm.mem_write(name, f"EFFECT_{capacity-1}\0".encode("ascii"))
        assert call("fx_index", 1, 27, table, capacity, name, 0) == capacity - 1 and not trap
        vm.mem_write(name, b"missing\0")
        call("fx_index", 0, 27, table, capacity, name, 1)
        assert trap == [BASE + 0x12a13cf]  # Engine full-cache diagnostic; never executed.
        cases.extend([f"fx_index/{capacity}/{label}" for label in
            ("missing", "allocate", "case_insensitive", "next_free", "last_index", "full_rejected")])
    return {"schema": 1, "executable_sha256": metadata["sha256"], "method": "bounded Unicorn memory emulation",
        "cases_passed": len(cases), "cases": cases, "game_initialization_executed": False,
        "os_imports_executed": False, "substituted_imports": ["ASCII tolower"],
        "engine_hooks_qualified": False, "coop_tested": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.executable.resolve() == args.output.resolve():
        parser.error("The report must not overwrite the executable")
    result = run(args.executable.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf8")
    print(f"{result['cases_passed']} bounded Mac ABI cases passed; no game or OS initialization ran")


if __name__ == "__main__":
    main()
