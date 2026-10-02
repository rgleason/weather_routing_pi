#!/usr/bin/env python3
"""Add OpenCPN's required metadata.xml to a CPack Android plugin archive."""

import io
import re
import sys
import tarfile
import xml.etree.ElementTree as ET
from pathlib import Path


def main(package_path: Path, xml_path: Path, output_dir: Path) -> None:
    metadata = xml_path.read_bytes()
    root = ET.fromstring(metadata)
    assert root.findtext("target", "").strip() == "android-arm64"
    assert root.findtext("api-version", "").strip() == "1.21"
    match = re.match(r"(x?weather_routing_pi)-(\d+\.\d+\.\d+)-", package_path.name)
    assert match, "Unexpected Android package filename"
    package_name, package_version = match.groups()
    assert root.findtext("version", "").strip() in {
        package_version, f"{package_version}.0"
    }
    assert root.findtext("name", "").strip() == (
        "xWeatherRouting" if package_name.startswith("x") else "WeatherRouting"
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / package_path.name.replace(".tar.gz", "-import.tar.gz")
    with tarfile.open(package_path, "r:gz") as source, tarfile.open(
        output, "w:gz"
    ) as destination:
        info = tarfile.TarInfo("metadata.xml")
        info.size = len(metadata)
        info.mode = 0o644
        destination.addfile(info, io.BytesIO(metadata))
        names = set()
        for member in source:
            names.add(member.name)
            destination.addfile(member, source.extractfile(member) if member.isfile() else None)
    assert any(name.endswith(f"/lib/opencpn/lib{package_name}.so") for name in names)
    assert any("/data/shoreline/manifest.json" in name for name in names)
    print(output)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: package-android-import.py PACKAGE XML OUTPUT_DIR")
    main(*(Path(argument) for argument in sys.argv[1:]))
