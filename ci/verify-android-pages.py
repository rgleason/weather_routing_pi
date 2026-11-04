#!/usr/bin/env python3
"""Fail closed on Android ELF/page alignment defects in final release artifacts.

Accepts ELF files, APKs, plugin tarballs, or directories. Does not extract
archives or execute binaries. This is a build gate, not a runtime qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tarfile
import zipfile

PAGE = 16384


def audit_elf(name, data):
    errors = []
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError(f'{name}: expected little-endian ELF64')
    machine = struct.unpack_from('<H', data, 18)[0]
    if machine not in (183, 62):
        errors.append(f'unsupported ELF64 machine {machine}')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', data, 54)
    if phsize != 56 or not phnum or phoff + phsize * phnum > len(data):
        raise ValueError(f'{name}: invalid program header table')
    loads, relro = [], []
    for i in range(phnum):
        kind, flags, offset, address, _, filesz, memsz, align = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + i * phsize)
        if kind == 1:
            loads.append(dict(offset=offset, address=address, alignment=align,
                              size=memsz, flags=flags))
            if align < PAGE or align & (align - 1):
                errors.append(f'LOAD {i}: invalid 16 KB alignment {align}')
            if (address - offset) % PAGE:
                errors.append(f'LOAD {i}: offset/address incongruent at 16 KB')
            if filesz > memsz or offset + filesz > len(data):
                errors.append(f'LOAD {i}: invalid file extent')
        elif kind == 0x6474e552:
            end = address + memsz
            relro.append(dict(address=address, size=memsz, end=end))
    # Bionic rounds RELRO protection outwards. A non-aligned end is safe when
    # the RELRO occupies an entire LOAD and the next writable LOAD is beyond
    # the rounded boundary (as in current NDK libc++). Reject writable overlap,
    # rather than incorrectly rejecting those official prebuilt libraries.
    for region in relro:
        start, end = region['address'], region['end']
        protected_start = start // PAGE * PAGE
        protected_end = (end + PAGE - 1) // PAGE * PAGE
        for load in loads:
            left, right = load['address'], load['address'] + load['size']
            if not load['flags'] & 2:
                continue
            if max(left, protected_start) < min(right, start):
                errors.append('RELRO rounds over writable data before its start')
            if max(left, end) < min(right, protected_end):
                errors.append('RELRO rounds over writable data after its end')
    if not loads:
        errors.append('no LOAD segments')
    return dict(name=name, sha256=hashlib.sha256(data).hexdigest(),
                machine=machine, loads=loads, relro=relro, errors=errors)


def members(path):
    if path.is_dir():
        for child in sorted(path.rglob('*')):
            if child.is_file() and (child.suffix in ('.so', '.apk') or child.name.endswith('.tar.gz')):
                yield from members(child)
    elif zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for item in archive.infolist():
                if item.filename.endswith('.so'):
                    # ZIP packaging checks apply only to uncompressed APK libs.
                    data = archive.read(item)
                    yield f'{path}:{item.filename}', data
                    if path.suffix == '.apk' and item.compress_type == 0:
                        with path.open('rb') as stream:
                            stream.seek(item.header_offset)
                            header = stream.read(30)
                        fnlen, extralen = struct.unpack_from('<HH', header, 26)
                        offset = item.header_offset + 30 + fnlen + extralen
                        if offset % PAGE:
                            raise ValueError(f'{item.filename}: APK ZIP offset not 16 KB aligned')
    elif tarfile.is_tarfile(path):
        with tarfile.open(path) as archive:
            for item in archive:
                if item.isfile() and item.name.endswith('.so'):
                    yield f'{path}:{item.name}', archive.extractfile(item).read()
    else:
        yield str(path), path.read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', nargs='+', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    rows, errors = [], []
    for path in args.artifacts:
        try:
            count = len(rows)
            for name, data in members(path):
                rows.append(audit_elf(name, data))
            if len(rows) == count:
                errors.append(f'{path}: no native libraries found')
        except (ValueError, OSError, struct.error, tarfile.TarError,
                zipfile.BadZipFile) as exc:
            errors.append(str(exc))
    report = dict(page_size=PAGE, libraries=rows, errors=errors,
                  passed=bool(rows) and not errors and not any(r['errors'] for r in rows))
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    for row in rows:
        print(('FAIL' if row['errors'] else 'PASS') + ' ' + row['name'])
        for error in row['errors']:
            print('  ' + error)
    for error in errors:
        print('FAIL ' + error)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
