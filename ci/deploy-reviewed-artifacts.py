#!/usr/bin/env python3
"""Upload staged, hash-checked artifacts to the selected channel only."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
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
    publish_all_or_nothing(profile, items, directory)


ANSI = re.compile(r"\x1b\[[0-9;]*m")
CREATED = re.compile(r"Created: ([^/\s]+)/([^/\s]+)/([^/\s]+) \(([^)\s]+)\)")


def cloudsmith(*args, data=False):
    """Run the Cloudsmith CLI and return (code, output).

    Normally stdout and stderr are merged and echoed. With data=True only
    stdout is returned (the JSON document) and stderr is echoed.
    """
    result = subprocess.run(["cloudsmith", *args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE if data else subprocess.STDOUT, text=True)
    output = ANSI.sub("", result.stdout or "")
    echo = ANSI.sub("", (result.stderr or "") if data else output)
    if echo:
        print(echo, end="" if echo.endswith("\n") else "\n", flush=True)
    return result.returncode, output


def existing_versions(repository, name):
    """Versions of an exact package name already in the repository.

    Upload names carry the plugin version but not the build number
    (weather_routing_pi-1.36.5-debian-x86_64-12-x86_64-tarball), so any hit
    means this plugin version was already published by an earlier build.
    Cloudsmith ignores anchored regex queries (name:^...$ finds nothing), so
    search by plain name and compare names exactly here.
    """
    code, output = cloudsmith("list", "packages", repository, "-q", f"name:{name}",
                              "--page-all", "-F", "json", data=True)
    try:
        packages = json.loads(output)["data"] if code == 0 else None
    except (ValueError, KeyError, TypeError):
        packages = None
    if not isinstance(packages, list):
        raise ValueError(f"Could not check Cloudsmith for existing {name}; nothing uploaded")
    return sorted(p.get("version", "?") for p in packages if p.get("name") == name)


def push_arguments(profile, item, dry_run=False):
    # Waiting for sync means a package Cloudsmith fails to process counts
    # as a failed upload and is rolled back with the rest.
    return ["push", "raw", "--no-republish", *(["--dry-run"] if dry_run else []),
            "--name", item["name"], "--version", item["version"],
            "--summary", profile["name"] + " OpenCPN plugin",
            profile["repository"], item["file"]]


def publish_all_or_nothing(profile, items, directory):
    """Publish every file or none: check all first, roll back on any failure.

    Cloudsmith has no multi-package transaction, so the release is made
    atomic here. Nothing is uploaded until all 22 files pass the server-side
    upload check and none of them already exists; if any upload or sync then
    fails, every package this run created is deleted again.
    """
    repository = profile["repository"]
    for item in items:
        existing = existing_versions(repository, item["name"])
        if existing:
            raise ValueError(
                f"{item['name']} is already published in {repository} as {', '.join(existing)}; "
                "nothing uploaded. Bump the version in CMakeLists.txt before releasing again.")
        code, _ = cloudsmith(*push_arguments(profile, item, dry_run=True))
        if code:
            raise ValueError(f"Cloudsmith rejected the upload check for {item['name']}; nothing uploaded")
    print(f"Pre-flight passed for all {len(items)} files; uploading", flush=True)

    created = []
    try:
        for item in items:
            code, output = cloudsmith(*push_arguments(profile, item))
            match = CREATED.search(output)
            if match:
                created.append({"name": item["name"], "slug": match[3], "slug_perm": match[4]})
            if code or not match:
                raise RuntimeError(f"Upload of {item['name']} failed" +
                                   ("" if match else " (no package slug reported)"))
    except Exception:
        failed = []
        for package in reversed(created):
            code, _ = cloudsmith("delete", f"{repository}/{package['slug']}", "-y")
            if code:
                failed.append(package)
        (directory / "rollback.json").write_text(json.dumps(
            {"deleted": [p for p in created if p not in failed], "delete_failed": failed}, indent=2) + "\n")
        if failed:
            raise RuntimeError("Upload failed and these packages could NOT be rolled back; delete them "
                               "by hand before retrying: " + ", ".join(p["slug_perm"] for p in failed)) from None
        raise RuntimeError(f"Upload failed; rolled back all {len(created)} packages created by this run. "
                           "Nothing is published; re-run the job to retry.") from None
    (directory / "published.json").write_text(json.dumps(created, indent=2) + "\n")
    print(f"Published all {len(created)} files to {repository}", flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--channel", choices=publication.PROFILES, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        upload(args.directory, args.channel, args.dry_run)
    except (ValueError, RuntimeError) as error:
        raise SystemExit(f"ERROR: {error}")
