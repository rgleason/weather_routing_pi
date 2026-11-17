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
    assert defaults["run_catalogue_pr"] is False
    assert defaults["catalogue_phase"] == "upstream"
    assert defaults["catalogue_auto_pr"] == "off"
    assert defaults["catalogue_fork"].lower() != "opencpn/plugins"
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
        # Phase 1 always follows a publish and never sees a GitHub token.
        assert jobs["catalogue-pr-dry-run"]["requires"] == ["publish-reviewed"]
        assert "context" not in jobs["catalogue-pr-dry-run"]
        # Single-trigger mode: skipped unless explicitly enabled, and only
        # after the Cloudsmith publish and dry run have both succeeded.
        assert jobs["catalogue-pr-auto"] == {
            "job": "catalogue-pr-push", "name": "catalogue-pr-auto",
            "phase": "<< pipeline.parameters.catalogue_auto_pr >>",
            "context": "opencpn-plugins-pr", "requires": ["catalogue-pr-dry-run"],
            "filters": 'pipeline.parameters.catalogue_auto_pr != "off"'}
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
                                 "hold-for-publication-approval", "publish-reviewed",
                                 "catalogue-pr-dry-run", "catalogue-pr-auto"}
    # Phases 2/3 run only in their own explicitly triggered workflow, and only
    # the post-approval push job holds the GitHub token context.
    for name, workflow in config["workflows"].items():
        if name == "publish_catalogue_pr":
            assert "<< pipeline.parameters.run_catalogue_pr >>" in workflow["when"]["and"]
        else:
            assert {"not": "<< pipeline.parameters.run_catalogue_pr >>"} in workflow["when"]["and"]
    catalogue = {}
    for entry in config["workflows"]["publish_catalogue_pr"]["jobs"]:
        job, options = (entry, {}) if isinstance(entry, str) else next(iter(entry.items()))
        catalogue[job] = options
    assert set(catalogue) == {"catalogue-pr-review", "hold-for-catalogue-pr-approval", "catalogue-pr-push"}
    assert "context" not in catalogue["catalogue-pr-review"]
    assert catalogue["hold-for-catalogue-pr-approval"] == {
        "type": "approval", "requires": ["catalogue-pr-review"]}
    assert catalogue["catalogue-pr-push"] == {
        "phase": "<< pipeline.parameters.catalogue_phase >>",
        "context": "opencpn-plugins-pr", "requires": ["hold-for-catalogue-pr-approval"]}
    assert "context" not in config["jobs"]["publication-preflight"]
    assert "context" not in config["jobs"]["stage-publication"]
    print("Publication identity, wx334 catalogue/wx329 compatibility, both API matrices, approval and catalogue PR gates verified")


if __name__ == "__main__":
    check(Path(".circleci/config.yml"))
