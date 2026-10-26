"""Write tests/data/sample.nso: a tiny fake game executable for the tests.

Its code is compressed with the system's real LZ4 library, so the test checks
the decompressor against actual LZ4 output rather than a hand-made block.

    python3 tests/data/make_sample_nso.py
"""

import ctypes
import ctypes.util
import pathlib
import struct

NOP = bytes.fromhex("1F2003D5")
FUNC = bytes.fromhex("E0031F2A C0035FD6")  # mov w0, wzr ; ret      at 0x800
CALL_AT = 0x900                             # bl 0x800


def build_text() -> bytes:
    text = bytearray(NOP * 1024)  # 4 KiB
    text[0x800:0x800 + len(FUNC)] = FUNC
    imm26 = ((0x800 - CALL_AT) // 4) & 0x03FFFFFF
    text[CALL_AT:CALL_AT + 4] = struct.pack("<I", 0x94000000 | imm26)
    return bytes(text)


def lz4_compress(data: bytes) -> bytes:
    lib = ctypes.CDLL(ctypes.util.find_library("lz4") or "liblz4.so.1")
    bound = lib.LZ4_compressBound(len(data))
    out = ctypes.create_string_buffer(bound)
    n = lib.LZ4_compress_default(data, out, len(data), bound)
    assert n > 0
    return out.raw[:n]


def main() -> None:
    text = build_text()
    packed = lz4_compress(text)

    header = bytearray(0x100)
    header[0:4] = b"NSO0"
    struct.pack_into("<I", header, 0x0C, 0b001)                     # .text compressed
    struct.pack_into("<III", header, 0x10, 0x100, 0, len(text))     # file off, mem off, size
    header[0x40:0x60] = bytes(range(0xA0, 0xC0))                    # build id
    struct.pack_into("<I", header, 0x60, len(packed))               # compressed .text size

    out = pathlib.Path(__file__).with_name("sample.nso")
    out.write_bytes(bytes(header) + packed)
    print(f"{out}: {len(text)} bytes of code, {len(packed)} compressed")


if __name__ == "__main__":
    main()
