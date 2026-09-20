#!/usr/bin/env python3
"""Create an OpenCPN manual-import archive with embedded metadata.xml."""

import io
import os
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath


def embed(package_path: Path, xml_path: Path, output_path: Path) -> None:
    metadata = xml_path.read_bytes()
    root = ET.fromstring(metadata)
    version = (root.findtext("version") or "").strip()
    if not version or version not in package_path.name:
        raise ValueError("package filename and metadata version differ")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output_path.name}.", dir=output_path.parent
    )
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    try:
        payload_files = 0
        with tarfile.open(package_path, "r:gz") as source, tarfile.open(
            temporary_path, "w:gz"
        ) as destination:
            info = tarfile.TarInfo("metadata.xml")
            info.size = len(metadata)
            info.mode = 0o644
            destination.addfile(info, io.BytesIO(metadata))
            for member in source:
                if PurePosixPath(member.name).name == "metadata.xml":
                    continue
                source_file = source.extractfile(member) if member.isfile() else None
                if member.isfile():
                    payload_files += 1
                destination.addfile(member, source_file)
        if payload_files == 0:
            raise ValueError("package contains no payload files")
        os.replace(temporary_path, output_path)
    finally:
        temporary_path.unlink(missing_ok=True)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: embed-package-metadata.py PACKAGE XML OUTPUT")
    embed(*(Path(argument) for argument in sys.argv[1:]))
