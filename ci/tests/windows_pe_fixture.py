"""Minimal PE import-table fixture, not an executable Windows plugin."""
import struct


def pe_image(family="334", machine=0x8664):
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HH", data, 132, machine, 1)
    struct.pack_into("<H", data, 148, 240)
    struct.pack_into("<H", data, 152, 0x20b)
    struct.pack_into("<I", data, 260, 16)
    struct.pack_into("<II", data, 272, 0x1000, 60)
    struct.pack_into("<IIII", data, 400, 512, 0x1000, 512, 512)
    cursor = 640
    for index, name in enumerate((f"wxbase{family}u_vc14x_x64.dll",
                                  f"wxmsw{family}u_core_vc14x_x64.dll")):
        struct.pack_into("<I", data, 512 + index * 20 + 12, 0x1000 + cursor - 512)
        content = name.encode("ascii") + b"\0"
        data[cursor:cursor + len(content)] = content
        cursor += len(content)
    return bytes(data)
