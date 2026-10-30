#!/usr/bin/env python3
"""Validate and stage qualified packages for an explicit publication channel.

The historical command defaults to xWeatherRouting Alpha for compatibility.
Rick's CircleCI workflow uses prepare-publication-artifacts.py with a channel.
"""

import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile
import xml.etree.ElementTree as ET

from importlib.util import module_from_spec, spec_from_file_location

windows_spec = spec_from_file_location(
    "windows_package_abi", Path(__file__).with_name("windows_package_abi.py"))
windows_abi = module_from_spec(windows_spec)
windows_spec.loader.exec_module(windows_abi)

EXPECTED_TARGETS = {
    "trixie": ("debian-x86_64", "13", "x86_64"),
    "bookworm": ("debian-x86_64", "12", "x86_64"),
    "jammy": ("ubuntu-wx32-x86_64", "22.04", "x86_64"),
    "noble": ("ubuntu-x86_64", "24.04", "x86_64"),
    "bookworm-arm64": ("debian-arm64", "12", "arm64"),
    "flatpak-x86_64": ("flatpak-x86_64", "25.08", "x86_64"),
    "flatpak-aarch64": ("flatpak-aarch64", "25.08", "aarch64"),
    "windows-x86": ("msvc-wx32", "10.0.20348", "x86"),
    "windows-x64": ("msvc-64", "10", "x86_64"),
    "windows-x64-wx329": ("msvc-64", "10", "x86_64"),
    "macos-arm64": ("darwin-wx32", "15.3.2", "arm64"),
    "android-arm64": ("android-arm64", "16", "arm64"),
}
SUPPLEMENTAL_TARGETS = {"windows-x64-wx329"}
TARGETS = set(EXPECTED_TARGETS) - SUPPLEMENTAL_TARGETS
PACKAGE = "xweather_routing_pi"
REPOSITORY = "pob220/xweather-routing-alpha-oss"
PROFILES = {
    "production": {
        "package": "weather_routing_pi", "name": "WeatherRouting",
        "repository": "opencpn/weather-routing-prod",
        "source": "https://github.com/rgleason/weather_routing_pi",
        "allowed_sources": {"https://github.com/rgleason/weather_routing_pi",
                            "https://github.com/pob220/weather_routing_pi"},
    },
    "xweather-alpha": {
        "package": PACKAGE, "name": "xWeatherRouting", "repository": REPOSITORY,
        "source": "https://github.com/pob220/xweather_routing_pi",
        "allowed_sources": {"https://github.com/pob220/xweather_routing_pi",
                            "https://github.com/rgleason/weather_routing_pi"},
    },
}
SUMMARY = "Weather routing with six engines and a clear multi-polar boat editor."
PREVIOUS_SUMMARY = "Weather routing, passage planning and existing-route chart checks."
OLDER_SUMMARY = "Advanced weather routing with departure and arrival planning."
LEGACY_SUMMARY = "Advanced deterministic weather routing with departure and arrival planning."


def value(root, field):
    text = (root.findtext(field) or "").strip()
    if not text:
        raise ValueError(f"Missing metadata field: {field}")
    return text


def profile_for(channel):
    if channel not in PROFILES:
        raise ValueError(f"Unknown publication channel: {channel}")
    return PROFILES[channel]


def inspect_pair(directory, channel="xweather-alpha"):
    profile = profile_for(channel)
    package_name = profile["package"]
    archives = list(directory.glob("*.tar.gz"))
    if len(archives) != 1:
        raise ValueError(f"Expected exactly one archive: {directory}")
    archive = archives[0]
    if not archive.name.startswith(package_name + "-"):
        raise ValueError(f"Wrong archive identity: {archive.name}")
    metadata = archive.with_name(archive.name[:-7] + ".xml")
    if not metadata.is_file():
        match = re.match(rf"{package_name}-(\d+\.\d+\.\d+)-", archive.name)
        candidates = list(directory.glob(f"{package_name}-{match[1]}-*.xml")) if match else []
        if len(candidates) != 1:
            raise ValueError(f"No unique same-version XML: {archive}")
        metadata = candidates[0]
    root = ET.parse(metadata).getroot()
    if root.tag != "plugin" or value(root, "name") != profile["name"]:
        raise ValueError(f"Wrong plugin name: {metadata}")
    summary = value(root, "summary")
    if summary not in {SUMMARY, PREVIOUS_SUMMARY, OLDER_SUMMARY, LEGACY_SUMMARY}:
        raise ValueError(f"Unexpected catalogue summary: {metadata}")
    root.find("summary").text = SUMMARY
    if len(SUMMARY) > 72:
        raise ValueError(f"Catalogue summary exceeds 72 characters: {metadata}")
    if value(root, "api-version") != "1.21":
        raise ValueError(f"Unexpected stock API requirement: {metadata}")
    if value(root, "source") not in profile["allowed_sources"]:
        raise ValueError(f"Wrong source repository: {metadata}")
    version = value(root, "version")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version) or not archive.name.startswith(f"{package_name}-{version}-"):
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
    build_target = (root.findtext("build-target") or "").strip()
    if matrix_name.startswith("windows-") and build_target and build_target != "msvc":
        raise ValueError(f"Invalid Windows build-target: {build_target}; expected msvc")
    libraries = {f"lib{package_name}.so", f"lib{package_name}.dylib", f"{package_name}.dll"}
    other_package = "xweather_routing_pi" if channel == "production" else "weather_routing_pi"
    with tarfile.open(archive, "r:gz") as package:
        members = package.getmembers()
        if any(PurePosixPath(item.name).is_absolute() or ".." in PurePosixPath(item.name).parts for item in members):
            raise ValueError(f"Unsafe archive path: {archive}")
        if sum(item.isfile() and PurePosixPath(item.name).name in libraries for item in members) != 1:
            raise ValueError(f"Missing/duplicate {profile['name']} library: {archive}")
        if matrix_name.startswith("windows-x64"):
            library = next(item for item in members if item.isfile() and
                           PurePosixPath(item.name).name == package_name + ".dll")
            windows_abi.validate(package.extractfile(library).read(), matrix_name)
        if any(
            re.fullmatch(
                rf"(?:lib)?{other_package}\.(?:so|dylib|dll)",
                PurePosixPath(item.name).name,
            )
            or re.match(r"(?:lib)?g(?:test|mock)", PurePosixPath(item.name).name)
            for item in members
        ):
            raise ValueError(f"Other plugin identity or test library included: {archive}")
        embedded_members = [item for item in members if item.name == "metadata.xml"]
        if len(embedded_members) != 1:
            raise ValueError(f"Missing/duplicate embedded metadata: {archive}")
        embedded = package.extractfile(embedded_members[0]).read()
        embedded_root = ET.fromstring(embedded)
        for field in ("name", "version", "api-version", "source", "target", "target-version", "target-arch"):
            if value(embedded_root, field) != value(root, field):
                raise ValueError(f"Embedded metadata {field} mismatch: {archive}")
        for field in ("build-target", "build-gtk"):
            if (embedded_root.findtext(field) or "").strip() != (root.findtext(field) or "").strip():
                raise ValueError(f"Embedded metadata {field} mismatch: {archive}")
    return archive, root, version, target, metadata.name


def prepare(artifact_root, output, revision, build_number, channel="xweather-alpha",
            source_repository=None, compatibility_root=None):
    profile = profile_for(channel)
    package_name, repository = profile["package"], profile["repository"]
    source_repository = source_repository or profile["source"]
    if source_repository not in profile["allowed_sources"]:
        raise ValueError("Source repository does not match the publication channel")
    if not re.fullmatch(r"[0-9a-f]{7,40}", revision) or not re.fullmatch(r"\d+", build_number):
        raise ValueError("Expected commit SHA and numeric CircleCI build number")
    directories = {path.parent.name: path for path in artifact_root.glob("*/package")}
    if set(directories) != TARGETS:
        raise ValueError(f"Incomplete/unexpected matrix: {sorted(directories)}")
    pairs = [(name, *inspect_pair(directories[name], channel)) for name in sorted(TARGETS)]
    if len({pair[3] for pair in pairs}) != 1 or len({pair[4] for pair in pairs}) != len(TARGETS):
        raise ValueError("Mixed versions or duplicate platform metadata")
    if compatibility_root is not None:
        supplemental = {path.parent.name: path for path in compatibility_root.glob("*/package")}
        if set(supplemental) != SUPPLEMENTAL_TARGETS:
            raise ValueError(f"Incomplete/unexpected compatibility matrix: {sorted(supplemental)}")
        pairs += [(name, *inspect_pair(supplemental[name], channel)) for name in sorted(SUPPLEMENTAL_TARGETS)]
        if len({pair[3] for pair in pairs}) != 1:
            raise ValueError("Mixed versions in compatibility packages")
    output.mkdir(parents=True, exist_ok=False)
    embed_spec = spec_from_file_location("embed", Path(__file__).with_name("embed-package-metadata.py"))
    embed_module = module_from_spec(embed_spec)
    embed_spec.loader.exec_module(embed_module)
    uploads = []
    inputs = []
    for matrix_name, archive, root, version, target, metadata_name in pairs:
        cloud_version = f"{version}+{build_number}.{revision[:7]}"
        base = f"{package_name}-{version}-{'-'.join(target)}"
        destination = output
        catalogue = matrix_name not in SUPPLEMENTAL_TARGETS
        if not catalogue:
            # Both wx ABIs currently share the host label msvc-64. Publish
            # the legacy one for explicit import without mixing catalogue rows.
            base += "-wx329"
            destination = output / matrix_name
            destination.mkdir()
        url = f"https://dl.cloudsmith.io/public/{repository}/raw/names/{base}-tarball/versions/{cloud_version}/{archive.name}"
        root.find("source").text = source_repository
        if root.find("info-url") is not None:
            root.find("info-url").text = source_repository
        root.find("tarball-url").text = "\n    " + url + "\n  "
        staged_metadata = destination / metadata_name
        ET.ElementTree(root).write(staged_metadata, encoding="utf-8", xml_declaration=True)
        staged_archive = destination / archive.name
        embed_module.embed(archive, staged_metadata, staged_archive)
        with tarfile.open(staged_archive, "r:gz") as package:
            embedded = package.extractfile("metadata.xml").read()
        if ET.tostring(ET.fromstring(embedded)) != ET.tostring(ET.parse(staged_metadata).getroot()):
            raise ValueError(f"Embedded metadata differs: {staged_archive}")
        with archive.open("rb") as stream:
            input_digest = hashlib.file_digest(stream, "sha256").hexdigest()
        inputs.append({"file": archive.name, "sha256": input_digest,
                       "target": list(target)})
        for name, file in ((base + "-metadata", staged_metadata), (base + "-tarball", staged_archive)):
            with file.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            uploads.append({"name": name, "version": cloud_version, "catalogue": catalogue,
                            "file": str(file.resolve()), "sha256": digest})
    (output / "uploads.json").write_text(json.dumps(uploads, indent=2) + "\n")
    (output / "publication.json").write_text(json.dumps({
        "channel": channel, "repository": repository, "package": package_name,
        "source_repository": source_repository, "source_revision": revision,
        "inputs": inputs, "uploads": uploads,
    }, indent=2) + "\n")
    print(f"Publication: {profile['name']} -> {repository}; source {revision}")
    print(f"Validated {len(pairs)} importable platform pairs; {len(uploads)} uploads staged")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("revision")
    parser.add_argument("build_number")
    parser.add_argument("--compatibility-root", type=Path)
    arguments = parser.parse_args()
    prepare(arguments.artifact_root, arguments.output, arguments.revision, arguments.build_number,
            compatibility_root=arguments.compatibility_root)
