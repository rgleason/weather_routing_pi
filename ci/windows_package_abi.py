"""Inspect the Windows x64 DLL in a finished package without loading it."""
import re
import struct


def imports(data):
    """Read an AMD64 PE import table; XML and archive filenames are not proof."""
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError("Truncated Windows PE image")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("Expected a Windows PE DLL")
    pe, = unpack("<I", 0x3c)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid Windows PE signature")
    machine, count = unpack("<HH", pe + 4)
    optional_size, = unpack("<H", pe + 20)
    optional = pe + 24
    magic, = unpack("<H", optional)
    if machine != 0x8664 or magic != 0x20b:
        raise ValueError("Windows x64 catalogue packages require an AMD64 PE32+ DLL")
    if not 0 < count <= 96 or optional_size < 128:
        raise ValueError("Invalid Windows PE headers")
    directory_count, = unpack("<I", optional + 108)
    import_rva, import_size = unpack("<II", optional + 120)
    if directory_count < 2 or not import_rva or import_size < 20:
        raise ValueError("Windows PE import table is absent")
    sections = []
    for index in range(count):
        at = optional + optional_size + index * 40
        virtual_size, rva, raw_size, raw = unpack("<IIII", at + 8)
        sections.append((rva, raw_size, raw))

    def offset(rva):
        for start, size, raw in sections:
            if start <= rva < start + size:
                result = raw + rva - start
                if result < len(data):
                    return result
        raise ValueError("Windows PE import points outside its raw sections")

    table = offset(import_rva)
    result = []
    for index in range(import_size // 20):
        entry = unpack("<IIIII", table + index * 20)
        if not any(entry):
            return result
        start = offset(entry[3])
        end = data.find(b"\0", start, min(start + 256, len(data)))
        if end < 0:
            raise ValueError("Invalid Windows PE import name")
        try:
            result.append(data[start:end].decode("ascii").lower())
        except UnicodeDecodeError as error:
            raise ValueError("Invalid Windows PE import name") from error
    raise ValueError("Unterminated Windows PE import table")


def validate(data, target):
    family = {"windows-x64": "334", "windows-x64-wx329": "32"}[target]
    dependencies = imports(data)
    core = f"wxmsw{family}u_core_vc14x_x64.dll"
    wx = [name for name in dependencies if name.startswith("wx")]
    pattern = rf"wx(?:base|msw){family}u(?:_[a-z]+)?_vc14x_x64\.dll"
    if core not in dependencies or any(not re.fullmatch(pattern, name) for name in wx):
        raise ValueError(f"Wrong wxWidgets ABI for {target}; expected {core}")
    return dependencies
