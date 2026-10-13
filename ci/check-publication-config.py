#!/usr/bin/env python3
"""Check release graph invariants after parsing YAML aliases."""
from pathlib import Path
import yaml


def check(path):
    config = yaml.safe_load(path.read_text())
    defaults = {name: value["default"] for name, value in config["parameters"].items()}
    assert defaults["build_xweather_identity"] is False
    assert defaults["run_workflow_deploy"] is False
    assert defaults["publication_channel"] == "production"
    for name in ("build_and_publish_reviewed", "publish_validated_artifacts"):
        workflow = config["workflows"][name]
        jobs = {}
        for entry in workflow["jobs"]:
            if isinstance(entry, str):
                jobs[entry] = {}
            else:
                job, options = next(iter(entry.items()))
                jobs[options.get("name", job)] = dict(options, job=job)
        assert jobs["hold-for-publication-approval"]["requires"] == ["stage-publication"]
        assert jobs["publish-reviewed"]["requires"] == ["hold-for-publication-approval"]
        assert "context" in jobs["publish-reviewed"]
        assert "context" not in jobs["stage-publication"]
        if name == "build_and_publish_reviewed":
            builds = {job for job, options in jobs.items() if options.get("job") in (
                "linux-catalogue", "flatpak", "windows-x86", "windows-x64",
                "macos-universal", "android-arm64")}
            assert len(builds) == 22
            assert set(jobs["stage-publication"]["requires"]) == builds
            assert all(jobs[job]["requires"] == ["publication-preflight"] for job in builds)
        else:
            assert set(jobs) == {"publication-preflight", "stage-publication",
                                 "hold-for-publication-approval", "publish-reviewed"}
    assert "context" not in config["jobs"]["publication-preflight"]
    assert "context" not in config["jobs"]["stage-publication"]
    print("Native defaults, both complete API matrices, prepared approval and rebuild-free promotion verified")


if __name__ == "__main__":
    check(Path(".circleci/config.yml"))
