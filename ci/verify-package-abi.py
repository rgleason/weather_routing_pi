#!/usr/bin/env python3
"""Check generated and embedded package labels against OpenCPN host ABIs."""
import argparse
import importlib.util
from pathlib import Path
import tarfile
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "alpha_targets", Path(__file__).with_name("prepare-alpha-artifacts.py"))
targets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(targets)

ALIASES = {"macos": "macos-arm64", "MSVC": "windows-x86",
           "MSVC-x64": "windows-x64"}


def validate(root, target):
    target = ALIASES.get(target, target)
    expected = targets.EXPECTED_TARGETS[target]
    actual = tuple((root.findtext(key) or "").strip()
                   for key in ("target", "target-version", "target-arch"))
    if actual[0] != expected[0] or actual[2] != expected[2]:
        raise ValueError(f"OpenCPN ABI mismatch for {target}: {actual}; expected {expected}")
    if target.startswith("windows-"):
        version_ok = actual[1].split(".")[0] == "10"
    elif target == "macos-arm64":
        version_ok = actual[1].split(".")[0].isdigit() and int(actual[1].split(".")[0]) >= 11
    else:
        version_ok = actual[1] == expected[1]
    if not version_ok:
        raise ValueError(f"OpenCPN runtime version mismatch for {target}: {actual}")
    if (root.findtext("api-version") or "").strip() not in {"1.21", "1.22"}:
        raise ValueError("Unsupported package API requirement")
    return actual


def verify(directory, target):
    metadata = {}
    # Validate the delivered files, not CPack's intermediate staging copies.
    for path in directory.glob("*weather_routing_pi-*.xml"):
        root = ET.parse(path).getroot()
        if root.tag != "plugin":
            continue
        validate(root, target)
        metadata[ET.tostring(root)] = path
    if not metadata:
        raise ValueError(f"No generated plugin metadata in {directory}")
    archives = list(directory.glob("*weather_routing_pi-*.tar.gz"))
    if not archives:
        raise ValueError(f"No plugin archive in {directory}")
    for path in archives:
        with tarfile.open(path, "r:gz") as package:
            if package.getnames().count("metadata.xml") != 1:
                raise ValueError(f"Missing/duplicate embedded metadata: {path}")
            root = ET.fromstring(package.extractfile("metadata.xml").read())
            selected = ALIASES.get(target, target)
            if selected.startswith("windows-x64"):
                libraries = [m for m in package if m.isfile() and
                             Path(m.name).name.endswith("weather_routing_pi.dll")]
                if len(libraries) != 1:
                    raise ValueError("Expected exactly one Windows plugin DLL")
                targets.windows_abi.validate(package.extractfile(libraries[0]).read(), selected)
        validate(root, target)
        if ET.tostring(root) not in metadata:
            raise ValueError(f"Embedded metadata differs from generated metadata: {path}")
    print(f"OpenCPN ABI labels and embedded metadata verified: {target}, {len(archives)} archive(s)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--target", required=True, choices=sorted(set(targets.EXPECTED_TARGETS) | set(ALIASES)))
    args = parser.parse_args()
    verify(args.directory, args.target)
