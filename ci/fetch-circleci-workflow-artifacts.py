#!/usr/bin/env python3
"""Recover package artifacts from a completed CircleCI workflow."""

import argparse
import json
from pathlib import Path, PurePosixPath
import time
from urllib.parse import quote, urlparse
from urllib.request import urlopen


TARGETS = {
    "debian13-x86_64": "trixie",
    "debian12-x86_64": "bookworm",
    "ubuntu22.04-x86_64": "jammy",
    "ubuntu24.04-x86_64": "noble",
    "debian12-arm64": "bookworm-arm64",
    "flatpak25.08-x86_64": "flatpak-x86_64",
    "flatpak25.08-aarch64": "flatpak-aarch64",
    "windows-x86": "windows-x86",
    "macos-universal": "macos-arm64",
}


def fetch_json(url):
    with urlopen(url, timeout=30) as response:
        return json.load(response)


def download(url, destination):
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname != "output.circle-artifacts.com":
        raise ValueError(f"Unexpected artifact URL: {url}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        try:
            with urlopen(url, timeout=60) as response, destination.open("wb") as output:
                while block := response.read(1024 * 1024):
                    output.write(block)
            return
        except OSError:
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)


def recover(workflow_id, output):
    jobs_url = f"https://circleci.com/api/v2/workflow/{quote(workflow_id)}/job"
    jobs = fetch_json(jobs_url)["items"]
    selected = {job["name"]: job for job in jobs if job["name"] in TARGETS}
    if set(selected) != set(TARGETS):
        raise ValueError(f"Incomplete workflow build matrix: {sorted(selected)}")
    if any(job["status"] != "success" for job in selected.values()):
        raise ValueError("Recovery requires every source build to be successful")

    for job_name, target in TARGETS.items():
        job = selected[job_name]
        project = quote(job["project_slug"], safe="/")
        artifacts_url = (
            f"https://circleci.com/api/v1.1/project/{project}/"
            f"{job['job_number']}/artifacts"
        )
        artifacts = fetch_json(artifacts_url)
        prefix = PurePosixPath("artifacts", target, "package")
        packages = []
        for artifact in artifacts:
            path = PurePosixPath(artifact["path"])
            if path.parent == prefix and (
                path.name.endswith(".tar.gz") or path.name.endswith(".xml")
            ):
                packages.append(artifact)
        local_package = output / prefix
        local_archives = list(local_package.glob("xweather_routing_pi-*.tar.gz"))
        local_metadata = list(local_package.glob("xweather_routing_pi-*.xml"))
        if not packages and len(local_archives) == 1 and len(local_metadata) == 1:
            print(f"Using current workflow artifact for {job_name}")
            continue
        if len(packages) != 2:
            raise ValueError(f"Expected archive and XML for {job_name}: {packages}")
        for artifact in packages:
            destination = output / artifact["path"]
            download(artifact["url"], destination)
            print(f"Recovered {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workflow_id")
    parser.add_argument("output", type=Path, nargs="?", default=Path("."))
    arguments = parser.parse_args()
    recover(arguments.workflow_id, arguments.output)
