#!/usr/bin/env python3
"""Upload staged, hash-checked artifacts to the selected channel only."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

spec = importlib.util.spec_from_file_location(
    "publication", Path(__file__).with_name("prepare-alpha-artifacts.py"))
publication = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publication)


def upload(directory, channel, dry_run=False):
    directory = directory.resolve()
    profile = publication.profile_for(channel)
    manifest = json.loads((directory / "publication.json").read_text())
    if (manifest["channel"], manifest["repository"], manifest["package"]) != (
            channel, profile["repository"], profile["package"]):
        raise ValueError("Staged publication identity/destination differs from selected channel")
    items = manifest["uploads"]
    if len(items) != 2 * len(publication.TARGETS):
        raise ValueError("Publication does not contain the complete platform matrix")
    if len({item["file"] for item in items}) != len(items):
        raise ValueError("Duplicate upload file")
    # Check every file before the first upload, so a changed later file cannot
    # produce an incomplete release.
    for item in items:
        path = Path(item["file"]).resolve()
        if path.parent != directory or not item["name"].startswith(profile["package"] + "-"):
            raise ValueError("Unexpected publication file or package name")
        with path.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        if checksum != item["sha256"]:
            raise ValueError("Staged file changed after preparation")
    print(f"{'Dry run' if dry_run else 'Publishing'}: {profile['name']} -> {profile['repository']}")
    if dry_run:
        return
    if not os.environ.get("CLOUDSMITH_API_KEY"):
        raise ValueError("Deployment context has no CLOUDSMITH_API_KEY")
    for item in items:
        subprocess.run([
            "cloudsmith", "push", "raw", "--no-republish", "--no-wait-for-sync",
            "--name", item["name"], "--version", item["version"],
            "--summary", profile["name"] + " OpenCPN plugin",
            profile["repository"], item["file"],
        ], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--channel", choices=publication.PROFILES, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    upload(args.directory, args.channel, args.dry_run)
