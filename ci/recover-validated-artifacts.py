#!/usr/bin/env python3
"""Recover exact packages from a qualified workflow without rebuilding them."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import time
from urllib.parse import quote, urlparse
from urllib.request import urlopen

spec = importlib.util.spec_from_file_location(
    "publication", Path(__file__).with_name("prepare-alpha-artifacts.py"))
publication = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publication)
TARGETS = {
    "debian13-x86_64": "trixie", "debian12-x86_64": "bookworm",
    "ubuntu22.04-x86_64": "jammy", "ubuntu24.04-x86_64": "noble",
    "debian12-arm64": "bookworm-arm64", "flatpak25.08-x86_64": "flatpak-x86_64",
    "flatpak25.08-aarch64": "flatpak-aarch64", "windows-x86": "windows-x86",
    "windows-x64": "windows-x64", "macos-universal": "macos-arm64",
    "android-arm64": "android-arm64",
}
# The supplemental wx ABI is qualified alongside catalogue builds, but its
# packages are kept out of the ordinary msvc-64 catalogue/publication matrix.
SUPPLEMENTAL_JOBS = {"windows-x64-wx329", "windows-x64-wx329-api122"}
EXPECTED_JOBS = set(TARGETS) | {name + "-api122" for name in TARGETS} | SUPPLEMENTAL_JOBS


def fetch_json(path):
    with urlopen("https://circleci.com" + path, timeout=30) as response:
        return json.load(response)


def check_source(workflow, pipeline, jobs, channel):
    publication.profile_for(channel)
    vcs = pipeline.get("vcs")
    if vcs is None and channel == "xweather-alpha" and workflow["project_slug"].startswith("circleci/"):
        # GitHub App projects expose checkout provenance here instead of vcs.
        # Keep production restricted to Rick's existing master qualification.
        git = pipeline.get("trigger_parameters", {}).get("git", {})
        vcs = {"origin_repository_url": git.get("repo_url", ""),
               "branch": git.get("branch", ""), "revision": git.get("checkout_sha", "")}
    vcs = vcs or {}
    repository = vcs.get("origin_repository_url", "").removesuffix(".git")
    revision = vcs.get("revision", "")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Source workflow has no exact commit revision")
    if channel == "production":
        if repository != "https://github.com/rgleason/weather_routing_pi" or vcs.get("branch") != "master":
            raise ValueError("Production requires qualified rgleason/weather_routing_pi master")
    elif repository not in publication.profile_for(channel)["allowed_sources"]:
        raise ValueError("Source repository does not match the selected channel")
    selected = [job for job in jobs if job["name"] in EXPECTED_JOBS]
    if len(selected) != len(EXPECTED_JOBS) or {job["name"] for job in selected} != EXPECTED_JOBS:
        raise ValueError("Source workflow must contain both complete API build matrices")
    if any(job["status"] != "success" for job in selected):
        raise ValueError("Every API 1.21 and 1.22 build must pass before publication")
    if any(job["project_slug"] != workflow["project_slug"] for job in selected):
        raise ValueError("Source workflow mixes projects")
    return repository, revision, {job["name"]: job for job in selected}


def download(artifact, destination):
    parsed = urlparse(artifact["url"])
    if parsed.scheme != "https" or parsed.hostname != "output.circle-artifacts.com":
        raise ValueError("Unexpected artifact download service")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".partial")
    try:
        for attempt in range(3):
            try:
                with urlopen(artifact["url"], timeout=45) as response, temporary.open("wb") as stream:
                    while block := response.read(1024 * 1024):
                        stream.write(block)
                temporary.replace(destination)
                return
            except OSError:
                if attempt == 2:
                    raise RuntimeError("Artifact download failed after three attempts") from None
                time.sleep(attempt + 1)
    finally:
        temporary.unlink(missing_ok=True)


def unique_artifact(artifacts, path):
    matches = [item for item in artifacts if item["path"] == str(path)]
    if len(matches) != 1:
        raise ValueError(f"Missing or duplicate qualification artifact: {path}")
    return matches[0]


def check_runtime(receipt, package, flatpak):
    routes = receipt.get("routes", [])
    expected = 6 if flatpak else 7
    flags = ("normal_profile", "import_passed", "host_bundled_data_lookup_checked") if flatpak else (
        "normal_profile", "imported_by_stock_host", "data_lookup_checked",
        "orderly_host_exit", "reinstall_preserves_personal_data")
    if not all(receipt.get(flag) is True for flag in flags):
        raise ValueError("Source runtime receipt does not qualify normal installation")
    if len(routes) != expected or any(route.get("status") != "complete" or route.get("exit_code") != 0
                                     for route in routes):
        raise ValueError("Source runtime receipt contains incomplete routing cases")
    if not flatpak and receipt.get("identity") != package:
        raise ValueError("Source runtime identity differs from publication")
    return receipt.get("package_sha256")


def recover(workflow_id, output, channel):
    if not re.fullmatch(r"[0-9a-fA-F-]{36}", workflow_id):
        raise ValueError("Expected a CircleCI workflow UUID")
    workflow = fetch_json(f"/api/v2/workflow/{workflow_id}")
    pipeline = fetch_json(f"/api/v2/pipeline/{workflow['pipeline_id']}")
    response = fetch_json(f"/api/v2/workflow/{workflow_id}/job")
    if response.get("next_page_token"):
        raise ValueError("Unexpected paginated workflow; cannot establish its full qualification")
    repository, revision, jobs = check_source(workflow, pipeline, response["items"], channel)
    output.mkdir(parents=True, exist_ok=False)
    profile = publication.profile_for(channel)
    packages, runtime = [], []
    for job_name, target in TARGETS.items():
        job = jobs[job_name]
        project = quote(job["project_slug"], safe="/")
        artifacts = fetch_json(f"/api/v1.1/project/{project}/{job['job_number']}/artifacts")
        prefix = PurePosixPath("artifacts", target, "package")
        # Unix retention also includes the stock host's source archive,
        # appdata XML and test reports. Only the selected plugin is a pair.
        pair = [item for item in artifacts if PurePosixPath(item["path"]).parent == prefix and
                PurePosixPath(item["path"]).name.startswith(profile["package"] + "-") and
                item["path"].endswith((".tar.gz", ".xml"))]
        if len(pair) != 2:
            raise ValueError(f"Expected one final archive and XML for {job_name}")
        for item in pair:
            download(item, output / item["path"])
        directory = output / prefix
        archive, _, _, _, _ = publication.inspect_pair(directory, channel)
        with archive.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        if not job_name.startswith("android"):
            relative = PurePosixPath("artifacts", target,
                                    "runtime" if job_name.startswith("flatpak") else "normal-runtime",
                                    "acceptance.json")
            download(unique_artifact(artifacts, relative), output / relative)
            receipt = json.loads((output / relative).read_text())
            expected_digest = check_runtime(receipt, profile["package"], job_name.startswith("flatpak"))
            if receipt.get("package") != archive.name or (expected_digest and expected_digest != checksum):
                raise ValueError("Final package differs from its runtime qualification receipt")
            runtime.append({"job": job_name, "routes": len(receipt["routes"])})
        manifests = [item for item in artifacts if item["path"] == str(prefix / "SHA256SUMS")]
        if manifests:
            download(unique_artifact(artifacts, prefix / "SHA256SUMS"), directory / "SHA256SUMS")
            lines = (directory / "SHA256SUMS").read_text().splitlines()
            if not any(line.startswith(checksum + " ") and line.rstrip().endswith(archive.name) for line in lines):
                raise ValueError("Final package differs from its recorded SHA256 manifest")
        packages.extend({"path": item["path"], "sha256": hashlib.sha256((output / item["path"]).read_bytes()).hexdigest()}
                        for item in pair)
        print(f"Qualified {job_name}: {archive.name} {checksum}", flush=True)
    receipt = {"channel": channel, "workflow_id": workflow_id,
               "pipeline_number": workflow["pipeline_number"], "repository": repository,
               "revision": revision, "jobs": sorted(EXPECTED_JOBS),
               "packages": packages, "runtime": runtime}
    (output / "validated-source.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"Qualified source {repository} @ {revision}; {len(EXPECTED_JOBS)} passing build jobs", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workflow_id")
    parser.add_argument("output", type=Path)
    parser.add_argument("--channel", choices=publication.PROFILES, required=True)
    args = parser.parse_args()
    recover(args.workflow_id, args.output, args.channel)
