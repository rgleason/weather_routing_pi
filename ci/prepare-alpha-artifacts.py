#!/usr/bin/env python3
"""Validate, URL-resolve and stage importable xWeatherRouting packages."""

import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
import xml.etree.ElementTree as ET

from importlib.util import module_from_spec, spec_from_file_location


EXPECTED_TARGETS = {
    "trixie": ("debian-x86_64", "13", "x86_64"),
    "bookworm": ("debian-x86_64", "12", "x86_64"),
    "jammy": ("ubuntu-x86_64", "22.04", "x86_64"),
    "noble": ("ubuntu-x86_64", "24.04", "x86_64"),
    "bookworm-arm64": ("debian-arm64", "12", "arm64"),
    "flatpak-x86_64": ("flatpak-32-x86_64", "25.08", "x86_64"),
    "flatpak-aarch64": ("flatpak-32-aarch64", "25.08", "aarch64"),
    "windows-x86": ("msvc-wx32", "10.0.20348", "x86"),
    "macos-arm64": ("darwin-wx32", "15.3.2", "arm64"),
}
TARGETS = set(EXPECTED_TARGETS)
PACKAGE = "xweather_routing_pi"
REPOSITORY = "pob220/xweather-routing-alpha-oss"
SUMMARY = "Advanced weather routing with departure and arrival planning."
LEGACY_SUMMARY = "Advanced deterministic weather routing with departure and arrival planning."


def value(root, field):
    text = (root.findtext(field) or "").strip()
    if not text:
        raise ValueError(f"Missing metadata field: {field}")
    return text


def inspect_pair(directory):
    archives = list(directory.glob("*.tar.gz"))
    if len(archives) != 1:
        raise ValueError(f"Expected exactly one archive: {directory}")
    archive = archives[0]
    if not archive.name.startswith(PACKAGE + "-"):
        raise ValueError(f"Wrong archive identity: {archive.name}")
    metadata = archive.with_name(archive.name[:-7] + ".xml")
    if not metadata.is_file():
        match = re.match(rf"{PACKAGE}-(\d+\.\d+\.\d+\.\d+)-", archive.name)
        candidates = list(directory.glob(f"{PACKAGE}-{match[1]}-*.xml")) if match else []
        if len(candidates) != 1:
            raise ValueError(f"No unique same-version XML: {archive}")
        metadata = candidates[0]
    root = ET.parse(metadata).getroot()
    if root.tag != "plugin" or value(root, "name") != "xWeatherRouting":
        raise ValueError(f"Wrong plugin name: {metadata}")
    summary = value(root, "summary")
    if summary not in {SUMMARY, LEGACY_SUMMARY}:
        raise ValueError(f"Unexpected catalogue summary: {metadata}")
    root.find("summary").text = SUMMARY
    if len(SUMMARY) > 72:
        raise ValueError(f"Catalogue summary exceeds 72 characters: {metadata}")
    if value(root, "api-version") != "1.21":
        raise ValueError(f"Unexpected stock API requirement: {metadata}")
    if value(root, "source") != "https://github.com/pob220/xweather_routing_pi":
        raise ValueError(f"Wrong source repository: {metadata}")
    version = value(root, "version")
    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", version) or not archive.name.startswith(f"{PACKAGE}-{version}-"):
        raise ValueError(f"Archive/XML version mismatch: {archive}")
    target = tuple(value(root, key) for key in ("target", "target-version", "target-arch"))
    if any(not re.fullmatch(r"[\w.+-]+", item) for item in target):
        raise ValueError(f"Invalid target: {target}")
    matrix_name = directory.parent.name
    if target != EXPECTED_TARGETS.get(matrix_name):
        raise ValueError(
            f"Unexpected target for {matrix_name}: {target}; "
            f"expected {EXPECTED_TARGETS.get(matrix_name)}"
        )
    libraries = {f"lib{PACKAGE}.so", f"lib{PACKAGE}.dylib", f"{PACKAGE}.dll"}
    with tarfile.open(archive, "r:gz") as package:
        members = package.getmembers()
        if any(PurePosixPath(item.name).is_absolute() or ".." in PurePosixPath(item.name).parts for item in members):
            raise ValueError(f"Unsafe archive path: {archive}")
        if sum(item.isfile() and PurePosixPath(item.name).name in libraries for item in members) != 1:
            raise ValueError(f"Missing/duplicate xWeatherRouting library: {archive}")
        if any(
            re.fullmatch(
                r"(?:lib)?weather_routing_pi\.(?:so|dylib|dll)",
                PurePosixPath(item.name).name,
            )
            or re.match(r"(?:lib)?g(?:test|mock)", PurePosixPath(item.name).name)
            for item in members
        ):
            raise ValueError(f"Standard plugin or test library included: {archive}")
        embedded_members = [item for item in members if item.name == "metadata.xml"]
        if len(embedded_members) != 1:
            raise ValueError(f"Missing/duplicate embedded metadata: {archive}")
        embedded = package.extractfile(embedded_members[0]).read()
        if (ET.fromstring(embedded).findtext("version") or "").strip() != version:
            raise ValueError(f"Embedded metadata version mismatch: {archive}")
    return archive, root, version, target, metadata.name


def prepare(artifact_root, output, revision, build_number):
    if not re.fullmatch(r"[0-9a-f]{7,40}", revision) or not re.fullmatch(r"\d+", build_number):
        raise ValueError("Expected commit SHA and numeric CircleCI build number")
    directories = {path.parent.name: path for path in artifact_root.glob("*/package")}
    if set(directories) != TARGETS:
        raise ValueError(f"Incomplete/unexpected matrix: {sorted(directories)}")
    pairs = [inspect_pair(directories[name]) for name in sorted(TARGETS)]
    if len({pair[2] for pair in pairs}) != 1 or len({pair[3] for pair in pairs}) != len(TARGETS):
        raise ValueError("Mixed versions or duplicate platform metadata")
    output.mkdir(parents=True, exist_ok=False)
    embed_spec = spec_from_file_location("embed", Path(__file__).with_name("embed-package-metadata.py"))
    embed_module = module_from_spec(embed_spec)
    embed_spec.loader.exec_module(embed_module)
    uploads = []
    for archive, root, version, target, metadata_name in pairs:
        cloud_version = f"{version}+{build_number}.{revision[:7]}"
        base = f"{PACKAGE}-{version}-{'-'.join(target)}"
        url = f"https://dl.cloudsmith.io/public/{REPOSITORY}/raw/names/{base}-tarball/versions/{cloud_version}/{archive.name}"
        root.find("tarball-url").text = "\n    " + url + "\n  "
        staged_metadata = output / metadata_name
        ET.ElementTree(root).write(staged_metadata, encoding="utf-8", xml_declaration=True)
        staged_archive = output / archive.name
        embed_module.embed(archive, staged_metadata, staged_archive)
        with tarfile.open(staged_archive, "r:gz") as package:
            embedded = package.extractfile("metadata.xml").read()
        if ET.tostring(ET.fromstring(embedded)) != ET.tostring(ET.parse(staged_metadata).getroot()):
            raise ValueError(f"Embedded metadata differs: {staged_archive}")
        for name, file in ((base + "-metadata", staged_metadata), (base + "-tarball", staged_archive)):
            with file.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            uploads.append({"name": name, "version": cloud_version,
                            "file": str(file.resolve()), "sha256": digest})
    (output / "uploads.json").write_text(json.dumps(uploads, indent=2) + "\n")
    print(f"Validated {len(pairs)} importable platform pairs; {len(uploads)} uploads staged")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("revision")
    parser.add_argument("build_number")
    arguments = parser.parse_args()
    prepare(arguments.artifact_root, arguments.output, arguments.revision, arguments.build_number)
