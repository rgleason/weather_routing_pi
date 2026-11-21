"""Release regression tests for identities, destinations and qualified reuse."""
import hashlib
from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
import xml.etree.ElementTree as ET

from windows_pe_fixture import pe_image


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parents[1] / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


prepare = module("release_prepare", "prepare-alpha-artifacts.py")
embed = module("release_embed", "embed-package-metadata.py")
deploy = module("release_upload", "deploy-reviewed-artifacts.py")
recover = module("release_recover", "recover-validated-artifacts.py")
request = module("release_request", "check-publication-request.py")
stage = module("release_stage", "prepare-publication-artifacts.py")


class Publication(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        quiet = redirect_stdout(io.StringIO())
        quiet.__enter__()
        self.addCleanup(quiet.__exit__, None, None, None)

    def pair(self, target, channel="production", wx_family=None):
        profile = prepare.profile_for(channel)
        directory = self.root / "artifacts" / target / "package"
        directory.mkdir(parents=True)
        archive = directory / f"{profile['package']}-1.30.1-{target}.tar.gz"
        library = (profile["package"] + ".dll" if target.startswith("windows") else
                   "lib" + profile["package"] + (".dylib" if target == "macos-arm64" else ".so"))
        content = (pe_image(wx_family or ("32" if target.endswith("wx329") else "334"))
                   if target.startswith("windows-x64") else b"original library bytes")
        with tarfile.open(archive, "w:gz") as output:
            for name, data in [("lib/" + library, content),
                               ("share/nested/polar.pol", b"original polar bytes")]:
                item = tarfile.TarInfo(name)
                item.size = len(data)
                output.addfile(item, io.BytesIO(data))
        metadata = archive.with_name(archive.name[:-7] + ".xml")
        root = ET.Element("plugin", version="1")
        fields = {"name": profile["name"], "version": "1.30.1", "api-version": "1.21",
                  "summary": prepare.SUMMARY, "source": profile["source"],
                  "target": prepare.EXPECTED_TARGETS[target][0],
                  "target-version": prepare.EXPECTED_TARGETS[target][1],
                  "target-arch": prepare.EXPECTED_TARGETS[target][2],
                  "tarball-url": "https://invalid.example/placeholder", "info-url": profile["source"]}
        for name, value in fields.items():
            ET.SubElement(root, name).text = value
        ET.ElementTree(root).write(metadata)
        embed.embed(archive, metadata, archive)
        return directory, archive, metadata

    def matrix(self, channel="production"):
        for target in prepare.TARGETS:
            self.pair(target, channel)
        output = self.root / "publication"
        prepare.prepare(self.root / "artifacts", output, "abcdef1" * 5 + "abcde", "42", channel)
        return output

    def test_native_production_resolves_every_url_to_opencpn_and_preserves_payload(self):
        output = self.matrix()
        manifest = json.loads((output / "publication.json").read_text())
        self.assertEqual(manifest["repository"], "opencpn/weather-routing-prod")
        self.assertEqual(len(manifest["inputs"]), 11)
        for archive in output.glob("*.tar.gz"):
            with tarfile.open(archive) as package:
                metadata = ET.fromstring(package.extractfile("metadata.xml").read())
                self.assertEqual(metadata.findtext("name"), "WeatherRouting")
                self.assertEqual(metadata.findtext("source"), "https://github.com/rgleason/weather_routing_pi")
                self.assertIn("/opencpn/weather-routing-prod/", metadata.findtext("tarball-url"))
                self.assertEqual(package.extractfile("share/nested/polar.pol").read(), b"original polar bytes")

    def test_native_cannot_be_staged_as_xweather_alpha(self):
        directory, _, _ = self.pair("trixie")
        with self.assertRaisesRegex(ValueError, "Wrong archive identity"):
            prepare.inspect_pair(directory, "xweather-alpha")

    def test_wx329_dll_with_wx334_filename_and_valid_xml_cannot_be_staged(self):
        for target in prepare.TARGETS:
            directory, archive, metadata = self.pair(
                target, wx_family="32" if target == "windows-x64" else None)
            if target == "windows-x64":
                archive.rename(directory / "weather_routing_pi-1.30.1-msvc-64-x86_64-10.0-MSVC-x64-wx334.tar.gz")
                metadata.rename(directory / "weather_routing_pi-1.30.1-msvc-64-x86_64-10.0-MSVC-x64-wx334.xml")
        output = self.root / "publication"
        with self.assertRaisesRegex(ValueError, "wxWidgets ABI"):
            prepare.prepare(self.root / "artifacts", output, "abcdef1", "42", "production")
        self.assertFalse(output.exists())

    def test_build_verification_checks_the_delivered_dll(self):
        verify = module("verify_delivered", "verify-package-abi.py")
        directory, _, _ = self.pair("windows-x64", wx_family="32")
        with self.assertRaisesRegex(ValueError, "wxWidgets ABI"):
            verify.verify(directory, "windows-x64")

    def test_xweather_cannot_be_staged_as_native_production(self):
        directory, _, _ = self.pair("trixie", "xweather-alpha")
        with self.assertRaisesRegex(ValueError, "Wrong archive identity"):
            prepare.inspect_pair(directory, "production")

    def test_existing_android_filename_is_valid_without_renaming(self):
        directory, archive, metadata = self.pair("android-arm64")
        name = "weather_routing_pi-1.30.1-android-arm64-16-android-arm64"
        archive.rename(directory / (name + ".tar.gz"))
        metadata.rename(directory / (name + ".xml"))
        self.assertEqual(prepare.inspect_pair(directory, "production")[0].name, name + ".tar.gz")

    def test_embedded_identity_must_match_external_metadata(self):
        directory, archive, metadata = self.pair("trixie")
        root = ET.parse(metadata).getroot()
        root.find("source").text = "https://github.com/pob220/weather_routing_pi"
        ET.ElementTree(root).write(metadata)
        with self.assertRaisesRegex(ValueError, "Embedded metadata source mismatch"):
            prepare.inspect_pair(directory, "production")

    def test_invalid_windows_build_target_is_rejected_before_staging(self):
        directory, archive, metadata = self.pair("windows-x64")
        root = ET.parse(metadata).getroot()
        ET.SubElement(root, "build-target").text = "msvc-64"
        ET.ElementTree(root).write(metadata)
        embed.embed(archive, metadata, archive)
        with self.assertRaisesRegex(ValueError, "Invalid Windows build-target"):
            prepare.inspect_pair(directory, "production")

    def test_external_only_windows_build_target_fix_is_rejected(self):
        directory, archive, metadata = self.pair("windows-x64")
        root = ET.parse(metadata).getroot()
        ET.SubElement(root, "build-target").text = "msvc-64"
        ET.ElementTree(root).write(metadata)
        embed.embed(archive, metadata, archive)
        root.find("build-target").text = "msvc"
        ET.ElementTree(root).write(metadata)
        with self.assertRaisesRegex(ValueError, "build-target mismatch"):
            prepare.inspect_pair(directory, "production")

    def test_wrong_upload_channel_fails_before_invoking_cloudsmith(self):
        output = self.matrix()
        with patch.object(deploy.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "destination"):
                deploy.upload(output, "xweather-alpha")
            run.assert_not_called()

    def test_changed_last_file_prevents_every_upload(self):
        output = self.matrix()
        manifest = json.loads((output / "publication.json").read_text())
        Path(manifest["uploads"][-1]["file"]).write_bytes(b"changed after preparation")
        with patch.object(deploy.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "changed"):
                deploy.upload(output, "production")
            run.assert_not_called()

    def cloudsmith_fake(self, fail_upload=None, existing=None, fail_delete=False):
        calls = []

        def run(arguments, **_):
            calls.append(arguments)
            command = arguments[1]
            if command == "list":
                # Like Cloudsmith: a plain name search also returns near misses,
                # and an earlier build of the same plugin version has another
                # build number in its version string.
                name = arguments[arguments.index("-q") + 1].removeprefix("name:")
                data = [{"name": name + "-legacy", "version": "1.0.0+1.0000000"}]
                if name == existing:
                    data.append({"name": name, "version": "1.30.1+41.1234567"})
                return subprocess.CompletedProcess(arguments, 0, json.dumps({"data": data}), "")
            if command == "delete":
                return subprocess.CompletedProcess(arguments, 1 if fail_delete else 0, "", "")
            if "--dry-run" in arguments:
                return subprocess.CompletedProcess(arguments, 0, "OK\n", "")
            name = arguments[arguments.index("--name") + 1]
            uploads = sum(1 for call in calls if call[1] == "push" and "--dry-run" not in call)
            created = f"Created: opencpn/weather-routing-prod/{name[-20:]}-{uploads} (perm{uploads})\n"
            return subprocess.CompletedProcess(arguments, 1 if uploads == fail_upload else 0, created, "")
        return run, calls

    def test_native_upload_commands_use_only_the_production_repository(self):
        output = self.matrix()
        fake, calls = self.cloudsmith_fake()
        with patch.dict(deploy.os.environ, {"CLOUDSMITH_API_KEY": "test-fixture"}), \
                patch.object(deploy.subprocess, "run", side_effect=fake):
            deploy.upload(output, "production")
        uploads = [call for call in calls if call[1] == "push" and "--dry-run" not in call]
        self.assertEqual(len(uploads), 22)
        self.assertEqual(len(calls), 66)   # 22 existence checks, 22 upload checks, 22 uploads
        for call in calls:
            self.assertIn("opencpn/weather-routing-prod", " ".join(call))
        self.assertEqual(len(json.loads((output / "published.json").read_text())), 22)

    def test_nothing_uploads_until_every_file_passes_preflight(self):
        output = self.matrix()
        manifest = json.loads((output / "publication.json").read_text())
        fake, calls = self.cloudsmith_fake(existing=manifest["uploads"][-1]["name"])
        with patch.dict(deploy.os.environ, {"CLOUDSMITH_API_KEY": "test-fixture"}), \
                patch.object(deploy.subprocess, "run", side_effect=fake):
            with self.assertRaisesRegex(ValueError, "already published in .* as 1.30.1\\+41.1234567; nothing uploaded"):
                deploy.upload(output, "production")
        self.assertFalse([call for call in calls if call[1] == "push" and "--dry-run" not in call])

    def test_failed_upload_rolls_back_every_package_it_created(self):
        output = self.matrix()
        fake, calls = self.cloudsmith_fake(fail_upload=9)
        with patch.dict(deploy.os.environ, {"CLOUDSMITH_API_KEY": "test-fixture"}), \
                patch.object(deploy.subprocess, "run", side_effect=fake):
            with self.assertRaisesRegex(RuntimeError, "rolled back all 9 packages"):
                deploy.upload(output, "production")
        deletes = [call[2] for call in calls if call[1] == "delete"]
        self.assertEqual(len(deletes), 9)
        self.assertTrue(all(target.startswith("opencpn/weather-routing-prod/") for target in deletes))
        self.assertFalse((output / "published.json").exists())
        self.assertEqual(len(json.loads((output / "rollback.json").read_text())["deleted"]), 9)

    def test_failed_rollback_names_packages_to_delete_by_hand(self):
        output = self.matrix()
        fake, _ = self.cloudsmith_fake(fail_upload=3, fail_delete=True)
        with patch.dict(deploy.os.environ, {"CLOUDSMITH_API_KEY": "test-fixture"}), \
                patch.object(deploy.subprocess, "run", side_effect=fake):
            with self.assertRaisesRegex(RuntimeError, "could NOT be rolled back.*perm1"):
                deploy.upload(output, "production")

    def test_dry_run_needs_no_credentials_and_never_calls_cloudsmith(self):
        output = self.matrix()
        with patch.dict(deploy.os.environ, {}, clear=True), patch.object(deploy.subprocess, "run") as run:
            deploy.upload(output, "production", dry_run=True)
            run.assert_not_called()

    def test_reused_package_mutation_is_rejected_before_staging(self):
        _, archive, _ = self.pair("trixie")
        source = self.root / "validated-source.json"
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        source.write_text(json.dumps({"channel": "production", "packages": [{
            "path": str(archive.relative_to(self.root)), "sha256": checksum}]}))
        archive.write_bytes(b"replaced archive")
        with self.assertRaisesRegex(ValueError, "changed after qualification"):
            stage.stage(self.root / "artifacts", self.root / "output", "production", "42", source)
        self.assertFalse((self.root / "output").exists())


class QualifiedWorkflow(unittest.TestCase):
    def source(self):
        workflow = {"project_slug": "gh/rgleason/weather_routing_pi"}
        pipeline = {"vcs": {"origin_repository_url": "https://github.com/rgleason/weather_routing_pi",
                            "branch": "master", "revision": "a" * 40}}
        jobs = [{"name": name, "status": "success", "project_slug": workflow["project_slug"]}
                for name in recover.EXPECTED_JOBS]
        return workflow, pipeline, jobs

    def test_all_24_builds_can_qualify_even_when_publication_failed(self):
        workflow, pipeline, jobs = self.source()
        workflow["status"] = "failed"
        _, revision, selected = recover.check_source(workflow, pipeline, jobs, "production")
        self.assertEqual(revision, "a" * 40)
        self.assertEqual(len(selected), 24)

    def test_github_app_production_uses_checkout_provenance(self):
        _, _, jobs = self.source()
        workflow = {"project_slug": "circleci/organisation/project"}
        for job in jobs:
            job["project_slug"] = workflow["project_slug"]
        pipeline = {"trigger_parameters": {"git": {
            "repo_url": "https://github.com/rgleason/weather_routing_pi",
            "branch": "master", "checkout_sha": "a" * 40}}}
        repository, revision, selected = recover.check_source(workflow, pipeline, jobs, "production")
        self.assertEqual(repository, "https://github.com/rgleason/weather_routing_pi")
        self.assertEqual(revision, "a" * 40)
        self.assertEqual(len(selected), 24)
        for key, value in [("branch", "pull/714"),
                           ("repo_url", "https://github.com/pob220/weather_routing_pi")]:
            git = pipeline["trigger_parameters"]["git"]
            previous = git[key]
            git[key] = value
            with self.assertRaisesRegex(ValueError, "master"):
                recover.check_source(workflow, pipeline, jobs, "production")
            git[key] = previous

    def test_github_app_alpha_uses_checkout_provenance(self):
        workflow, _, jobs = self.source()
        workflow["project_slug"] = "circleci/organisation/project"
        for job in jobs:
            job["project_slug"] = workflow["project_slug"]
        pipeline = {"trigger_parameters": {"git": {
            "repo_url": "https://github.com/pob220/xweather_routing_pi",
            "branch": "release/xweather-1.35.1", "checkout_sha": "b" * 40}}}
        repository, revision, selected = recover.check_source(workflow, pipeline, jobs, "xweather-alpha")
        self.assertEqual(repository, "https://github.com/pob220/xweather_routing_pi")
        self.assertEqual(revision, "b" * 40)
        self.assertEqual(len(selected), 24)
        for field, wrong in (("checkout_sha", ""), ("repo_url", "https://github.com/other/project")):
            git = pipeline["trigger_parameters"]["git"]
            previous = git[field]
            git[field] = wrong
            with self.assertRaises(ValueError):
                recover.check_source(workflow, pipeline, jobs, "xweather-alpha")
            git[field] = previous
        with self.assertRaisesRegex(ValueError, "master"):
            recover.check_source(workflow, pipeline, jobs, "production")

    def test_missing_or_failed_legacy_wx329_build_prevents_promotion(self):
        for name in recover.SUPPLEMENTAL_JOBS:
            for missing in (False, True):
                workflow, pipeline, jobs = self.source()
                candidate = next(job for job in jobs if job["name"] == name)
                if missing:
                    jobs.remove(candidate)
                else:
                    candidate["status"] = "failed"
                with self.assertRaises(ValueError):
                    recover.check_source(workflow, pipeline, jobs, "production")

    def test_missing_or_failed_api122_build_prevents_promotion(self):
        for missing in (False, True):
            workflow, pipeline, jobs = self.source()
            candidate = next(job for job in jobs if job["name"].endswith("-api122"))
            if missing:
                jobs.remove(candidate)
            else:
                candidate["status"] = "failed"
            with self.assertRaises(ValueError):
                recover.check_source(workflow, pipeline, jobs, "production")

    def test_unreviewed_branch_or_other_repository_cannot_publish_production(self):
        for key, value in [("branch", "pull/714"),
                           ("origin_repository_url", "https://github.com/pob220/weather_routing_pi")]:
            workflow, pipeline, jobs = self.source()
            pipeline["vcs"][key] = value
            with self.assertRaisesRegex(ValueError, "master"):
                recover.check_source(workflow, pipeline, jobs, "production")

    def test_missing_revision_and_mixed_projects_are_rejected(self):
        workflow, pipeline, jobs = self.source()
        pipeline["vcs"]["revision"] = ""
        with self.assertRaises(ValueError):
            recover.check_source(workflow, pipeline, jobs, "production")
        workflow, pipeline, jobs = self.source()
        jobs[0]["project_slug"] = "gh/other/project"
        with self.assertRaisesRegex(ValueError, "mixes"):
            recover.check_source(workflow, pipeline, jobs, "production")

    def test_missing_normal_runtime_proof_cannot_qualify_a_package(self):
        with self.assertRaisesRegex(ValueError, "normal installation"):
            recover.check_runtime({"routes": []}, "weather_routing_pi", False)

    def test_missing_artifact_listing_recovers_the_retained_package_by_job_id(self):
        _, _, jobs = self.source()
        job = next(job for job in jobs if job["name"] == "flatpak25.08-x86_64")
        job.update(id="12345678-abcd-4567-89ab-123456789abc", job_number=42)
        prefix = "artifacts/flatpak-x86_64"
        receipt = Path(prefix) / "runtime/acceptance.json"
        archive = "weather_routing_pi-1.38.0-x86_64_flatpak-25.08.tar.gz"
        contents = {
            str(receipt): json.dumps({"package": archive}),
            f"{prefix}/package/{archive}": "validated archive",
            f"{prefix}/package/weather_routing_pi-1.38.0-flatpak-x86_64-25.08-flatpak.xml":
                "validated metadata",
        }

        def download(artifact, destination):
            path = str(destination.relative_to(output))
            self.assertEqual(artifact["url"],
                f"https://output.circle-artifacts.com/output/job/{job['id']}/artifacts/0/{path}")
            if path not in contents:
                raise HTTPError(artifact["url"], 404, "Not Found", None, None)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(contents[path])

        unavailable = HTTPError(None, 404, "Not Found", None, None)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch.object(recover, "fetch_json", side_effect=unavailable), \
                    patch.object(recover, "download", side_effect=download):
                pair = recover.recover_package(job, "flatpak-x86_64",
                    "weather_routing_pi", output, receipt)
                self.assertEqual([(output / path).read_text() for path in pair],
                                 ["validated archive", "validated metadata"])
                contents[str(receipt)] = json.dumps({"package": f"../{archive}"})
                with self.assertRaisesRegex(ValueError, "Invalid package filename"):
                    recover.recover_package(job, "flatpak-x86_64",
                        "weather_routing_pi", output, receipt)


class ReleaseRequest(unittest.TestCase):
    def test_default_native_request_and_explicit_alpha_request(self):
        request.check("production", False, "", "master", "weather_routing_pi")
        request.check("xweather-alpha", True, "", "master", "weather_routing_pi")

    def test_identity_mismatch_is_rejected_before_building(self):
        for channel, xweather in [("production", True), ("xweather-alpha", False)]:
            with self.assertRaisesRegex(ValueError, "build_xweather_identity"):
                request.check(channel, xweather, "", "master", "weather_routing_pi")

    def test_production_request_on_a_review_branch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "master"):
            request.check("production", False, "", "pull/714", "weather_routing_pi")


if __name__ == "__main__":
    unittest.main()
