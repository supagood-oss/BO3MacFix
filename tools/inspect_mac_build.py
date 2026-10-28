#!/usr/bin/env python3
"""Read-only Mac executable inspection. Runs on Windows, Linux, or macOS.

It never loads/executes the file and never writes to the game. The output is
evidence for mapping; it cannot promote a candidate address to a verified hook.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import uuid

X86_64 = 0x01000007


class InvalidMachO(ValueError):
    pass


def _range(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise InvalidMachO("A load command or segment points outside the executable")
    return data[offset:offset + size]


def _name(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", errors="replace")


def inspect(data: bytes) -> dict:
    original = data
    slice_offset = 0
    if data[:4] in (b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
        fat64 = data[:4] == b"\xca\xfe\xba\xbf"
        count = struct.unpack(">I", _range(data, 4, 4))[0]
        if count > 64:
            raise InvalidMachO("Unreasonable universal-binary architecture count")
        selected = None
        for index in range(count):
            record = _range(data, 8 + index * (32 if fat64 else 20), 32 if fat64 else 20)
            if fat64:
                cpu, subtype, offset, size, align, reserved = struct.unpack(">IIQQII", record)
            else:
                cpu, subtype, offset, size, align = struct.unpack(">IIIII", record)
            if cpu == X86_64:
                if selected is not None:
                    raise InvalidMachO("Multiple Intel slices; select a build explicitly")
                selected = (offset, size)
        if selected is None:
            raise InvalidMachO("This executable has no Intel x86_64 slice")
        slice_offset, size = selected
        data = _range(data, slice_offset, size)
    if data[:4] != b"\xcf\xfa\xed\xfe":
        raise InvalidMachO("Expected a little-endian 64-bit Mac executable")
    magic, cpu, subtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack(
        "<8I", _range(data, 0, 32))
    if cpu != X86_64 or filetype != 2:
        raise InvalidMachO("Expected the Intel Mac game executable (MH_EXECUTE)")
    command_end = 32 + sizeofcmds
    _range(data, 32, sizeofcmds)
    if ncmds > sizeofcmds // 8:
        raise InvalidMachO("Invalid load-command count")
    commands = []
    sections = []
    image_uuid = None
    symbols = []
    symtab = None
    offset = 32
    for _ in range(ncmds):
        cmd, length = struct.unpack("<II", _range(data, offset, 8))
        if length < 8 or length % 8 or length > command_end - offset:
            raise InvalidMachO("Invalid or truncated load command")
        raw = _range(data, offset, length)
        commands.append({"command": hex(cmd), "size": length})
        if cmd == 0x1b:
            if length < 24:
                raise InvalidMachO("Truncated UUID command")
            image_uuid = str(uuid.UUID(bytes=raw[8:24]))
        elif cmd == 0x19:
            if length < 72:
                raise InvalidMachO("Truncated segment command")
            _, _, segment, vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, segflags = struct.unpack(
                "<II16sQQQQIIII", raw[:72])
            if nsects > (length - 72) // 80:
                raise InvalidMachO("Section count exceeds segment command")
            _range(data, fileoff, filesize)
            for index in range(nsects):
                section = struct.unpack("<16s16sQQIIIIIIII", raw[72 + 80 * index:152 + 80 * index])
                name, segname, address, size, file_offset, align, reloff, nreloc, secflags, r1, r2, r3 = section
                zero_fill = secflags & 0xff in (1, 0xc, 0x12)
                if not zero_fill:
                    _range(data, file_offset, size)
                sections.append({"name": _name(name), "segment": _name(segname), "address": address,
                    "size": size, "file_offset": file_offset, "zero_fill": zero_fill})
        elif cmd == 2:
            if length < 24:
                raise InvalidMachO("Truncated symbol-table command")
            symtab = struct.unpack("<4I", raw[8:24])
        offset += length
    if offset != command_end:
        raise InvalidMachO("Load-command sizes disagree with the header")
    if symtab:
        symoff, nsyms, stroff, strsize = symtab
        strings = _range(data, stroff, strsize)
        _range(data, symoff, nsyms * 16)
        for index in range(nsyms):
            strx, kind, section, desc, value = struct.unpack("<IBBHQ", data[symoff + index * 16:symoff + (index + 1) * 16])
            if kind & 0xe0 or kind & 0xe != 0xe or not value or strx >= len(strings):
                continue
            end = strings.find(b"\0", strx)
            if end == -1:
                raise InvalidMachO("Unterminated symbol name")
            name = strings[strx:end].decode("utf8", errors="replace")
            if any(term in name for term in ("PM_", "BG_", "Jump_", "Scr_", "CScr_", "CanPause", "Cache_", "Dvar_", "Cmd_")):
                symbols.append({"name": name, "address": value})
    return {"schema": 1, "sha256": hashlib.sha256(original).hexdigest(),
        "sha1": hashlib.sha1(original).hexdigest(), "bytes": len(original),
        "slice_offset": slice_offset, "intel_slice_sha256": hashlib.sha256(data).hexdigest(),
        "uuid": image_uuid, "architecture": "x86_64", "sections": sections,
        "candidate_symbols": symbols, "mapping_verified": False,
        "note": "Read-only metadata. Candidate symbols do not qualify structures, calling conventions or patch sites."}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() == args.executable.resolve():
        parser.error("The report must not overwrite the input executable")
    try:
        result = inspect(args.executable.read_bytes())
    except (InvalidMachO, OSError) as error:
        parser.exit(1, f"Cannot inspect this build: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf8")
    print(f"Read-only build report saved to {args.output}")


if __name__ == "__main__":
    main()
