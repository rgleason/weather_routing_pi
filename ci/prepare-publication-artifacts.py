#!/usr/bin/env python3
"""Stage an explicit production or xWeatherRouting Alpha publication."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

spec = importlib.util.spec_from_file_location(
    "publication", Path(__file__).with_name("prepare-alpha-artifacts.py"))
publication = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publication)


def stage(artifacts, output, channel, build_number, source_receipt=None):
    if source_receipt:
        source = json.loads(source_receipt.read_text())
        if source["channel"] != channel:
            raise ValueError("Validated workflow channel differs from publication")
        for item in source["packages"]:
            path = (source_receipt.parent / item["path"]).resolve()
            if not path.is_relative_to(artifacts.resolve()):
                raise ValueError("Unexpected recovered package path")
            with path.open("rb") as stream:
                actual = publication.hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != item["sha256"]:
                raise ValueError("Recovered package changed after qualification")
        revision, repository = source["revision"], source["repository"]
    else:
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
        repository = publication.profile_for(channel)["source"]
    publication.prepare(artifacts, output, revision, build_number, channel, repository)
    if source_receipt:
        (output / "validated-source.json").write_bytes(source_receipt.read_bytes())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--channel", choices=publication.PROFILES, required=True)
    parser.add_argument("--build-number", required=True)
    parser.add_argument("--source-receipt", type=Path)
    args = parser.parse_args()
    stage(args.artifacts, args.output, args.channel, args.build_number, args.source_receipt)
