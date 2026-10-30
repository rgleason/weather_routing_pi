#!/usr/bin/env python3
"""Check release graph invariants after parsing YAML aliases."""
from pathlib import Path
import yaml


def check(path):
    config = yaml.safe_load(path.read_text())
    defaults = {name: value["default"] for name, value in config["parameters"].items()}
    assert (defaults["build_xweather_identity"], defaults["publication_channel"]) in {
        (False, "production"), (True, "xweather-alpha")}
    assert defaults["run_workflow_deploy"] is False
    windows = config["jobs"]["windows-x64"]["parameters"]
    assert windows["wx_version"]["default"] == "3.3.4"
    assert windows["sdk_pin"]["default"] == "ci/windows64-sdk.json"
    for name in ("validate", "validate_windows_only", "build_and_publish_reviewed"):
        # Both API header builds must keep wx334 in the catalogue and wx329
        # outside it, even if a workflow overrides the shared job defaults.
        variants = [options for entry in config["workflows"][name]["jobs"]
                    if isinstance(entry, dict) for job, options in entry.items()
                    if job == "windows-x64"]
        assert len(variants) == 4
        for options in variants:
            legacy = options.get("name", "").startswith("windows-x64-wx329")
            assert options.get("wx_version", "3.3.4") == ("3.2.9" if legacy else "3.3.4")
            assert options.get("target", "windows-x64") == ("windows-x64-wx329" if legacy else "windows-x64")
            assert options.get("sdk_pin", "ci/windows64-sdk.json") == (
                "ci/windows64-wx329-sdk.json" if legacy else "ci/windows64-sdk.json")
            assert options.get("artifact_root", "artifacts") == ("compatibility" if legacy else "artifacts")
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
            assert len(builds) == 24
            for suffix in ("", "-api122"):
                variant = jobs["windows-x64-wx329" + suffix]
                assert variant["wx_version"] == "3.2.9"
                assert variant["artifact_root"] == "compatibility"
            assert set(jobs["stage-publication"]["requires"]) == builds
            assert all(jobs[job]["requires"] == ["publication-preflight"] for job in builds)
        else:
            assert set(jobs) == {"publication-preflight", "stage-publication",
                                 "hold-for-publication-approval", "publish-reviewed"}
    assert "context" not in config["jobs"]["publication-preflight"]
    assert "context" not in config["jobs"]["stage-publication"]
    print("Publication identity, wx334 catalogue/wx329 compatibility, both API matrices and approval verified")


if __name__ == "__main__":
    check(Path(".circleci/config.yml"))
