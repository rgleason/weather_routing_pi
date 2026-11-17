"""Offline tests for the OpenCPN/plugins catalogue pull request phases."""
from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

from windows_pe_fixture import pe_image


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parents[1] / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


prepare = module("catalogue_prepare", "prepare-alpha-artifacts.py")
embed = module("catalogue_embed", "embed-package-metadata.py")
catalogue = module("catalogue_pr", "catalogue-pull-request.py")
SCHEMA = Path(__file__).parents[1] / "catalogue" / "ocpn-plugin.xsd"
GIT = ["git", "-c", "user.name=t", "-c", "user.email=t@invalid", "-c", "init.defaultBranch=master"]


def run(*args, cwd=None):
    subprocess.run([*GIT, *args], cwd=cwd, check=True, capture_output=True)


def catalogue_xml(version, target, name="WeatherRouting"):
    platform = prepare.EXPECTED_TARGETS[target]
    return (f"<?xml version='1.0' encoding='utf-8'?>\n<plugin version=\"1\">\n"
            f"  <name> {name} </name>\n  <version> {version} </version>\n  <release> 0 </release>\n"
            f"  <summary>x</summary>\n  <api-version> 1.21 </api-version>\n"
            f"  <open-source> yes </open-source>\n  <author> a </author>\n"
            f"  <source>https://github.com/rgleason/weather_routing_pi</source>\n"
            f"  <description> d </description>\n  <target>{platform[0]}</target>\n"
            f"  <target-version>{platform[1]}</target-version>\n  <target-arch>{platform[2]}</target-arch>\n"
            f"  <tarball-url>https://dl.cloudsmith.io/public/opencpn/weather-routing-prod/raw/x.tar.gz</tarball-url>\n"
            f"  <info-url>https://github.com/rgleason/weather_routing_pi</info-url>\n</plugin>\n")


class FakeGitHub:
    def __init__(self, upstream_write=False):
        self.calls, self.upstream_write = [], upstream_write

    def call(self, method, path, body=None):
        self.calls.append((method, path, body))
        if path == "/user":
            return {"login": "rgleason", "id": 7, "name": "Rick Gleason"}
        if path == "/repos/rgleason/plugins":
            return {"fork": True, "parent": {"full_name": "OpenCPN/plugins"}, "permissions": {"push": True}}
        if path == "/repos/OpenCPN/plugins":
            return {"permissions": {"push": self.upstream_write}}
        if "/pulls?" in path:
            return []
        if method == "POST" and path.endswith("/pulls"):
            return {"html_url": "https://github.com/example/pull/1"}
        raise AssertionError(path)


@unittest.skipUnless(shutil.which("xmllint") and shutil.which("git"), "needs xmllint and git")
class CataloguePullRequest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        quiet = redirect_stdout(io.StringIO())
        quiet.__enter__()
        self.addCleanup(quiet.__exit__, None, None, None)
        self.upstream = self.root / "upstream"
        (self.upstream / "metadata").mkdir(parents=True)
        (self.upstream / "tools").mkdir()
        shutil.copy(SCHEMA, self.upstream / "ocpn-plugin.xsd")
        self.url_tool(0)
        for target in prepare.TARGETS:
            platform = "-".join(prepare.EXPECTED_TARGETS[target])
            name = f"weather_routing_pi-1.36.5-weather_routing_pi-1.36.5-{platform}-metadata.xml"
            (self.upstream / "metadata" / name).write_text(catalogue_xml("1.36.5", target))
        (self.upstream / "metadata" / "other_pi-1.0.0-debian.xml").write_text(
            catalogue_xml("1.0.0", "bookworm", "Other"))
        run("init", "-q", str(self.upstream))
        run("add", ".", cwd=self.upstream)
        run("commit", "-qm", "base", cwd=self.upstream)

    def url_tool(self, code):
        tool = self.upstream / "tools" / "check-metadata-urls"
        tool.write_text(f"import sys\nprint('url check')\nsys.exit({code})\n")

    def publication(self, version="1.36.6", channel="production"):
        profile = prepare.profile_for(channel)
        artifacts = self.root / f"artifacts-{version}-{channel}"
        for target in prepare.TARGETS:
            directory = artifacts / target / "package"
            directory.mkdir(parents=True)
            archive = directory / f"{profile['package']}-{version}-{target}.tar.gz"
            library = (profile["package"] + ".dll" if target.startswith("windows") else
                       "lib" + profile["package"] + (".dylib" if target == "macos-arm64" else ".so"))
            data = pe_image("334") if target.startswith("windows-x64") else b"library"
            with tarfile.open(archive, "w:gz") as output:
                item = tarfile.TarInfo("lib/" + library)
                item.size = len(data)
                output.addfile(item, io.BytesIO(data))
            root = ET.Element("plugin", version="1")
            fields = {"name": profile["name"], "version": version, "release": "0",
                      "summary": prepare.SUMMARY, "api-version": "1.21", "open-source": "yes",
                      "author": "a", "source": profile["source"], "description": "d",
                      "target": prepare.EXPECTED_TARGETS[target][0],
                      "target-version": prepare.EXPECTED_TARGETS[target][1],
                      "target-arch": prepare.EXPECTED_TARGETS[target][2],
                      "tarball-url": "https://invalid.example/x", "info-url": profile["source"]}
            for key, value in fields.items():
                ET.SubElement(root, key).text = value
            metadata = archive.with_name(archive.name[:-7] + ".xml")
            ET.ElementTree(root).write(metadata)
            embed.embed(archive, metadata, archive)
        output = self.root / f"publication-{version}-{channel}"
        prepare.prepare(artifacts, output, "abcdef1" * 5 + "abcde", "4242", channel)
        return output

    def dry_run(self, publication, name="dry-run", **options):
        output = self.root / name
        catalogue.prepare(publication, output, "master", str(self.upstream),
                          options.get("url_wait", 0), options.get("require_live_urls", False))
        return output, json.loads((output / "report.json").read_text())

    def test_dry_run_replaces_only_same_plugin_targets_and_pushes_nothing(self):
        output, report = self.dry_run(self.publication())
        self.assertEqual(report["branch"], "weather_routing_pi-1.36.6-build4242")
        self.assertEqual(len(report["added"]), len(prepare.TARGETS))
        self.assertEqual(len(report["removed"]), len(prepare.TARGETS))
        self.assertTrue(all(item["version"] == "1.36.5" for item in report["removed"]))
        self.assertTrue(all(line.startswith(("A\tmetadata/weather_routing_pi-1.36.6-weather_routing_pi-1.36.6-",
                                             "D\tmetadata/weather_routing_pi-1.36.5-"))
                            for line in report["changes"]))
        self.assertEqual(len(list((output / "metadata").glob("*.xml"))), len(prepare.TARGETS))
        for name in ("catalogue.patch", "catalogue.diff", "pull-request.md", "SUMMARY.txt"):
            self.assertTrue((output / name).is_file(), name)
        self.assertEqual(report["phase"], "dry-run")
        self.assertEqual(report["warnings"], [])
        # Upstream itself is unchanged by a dry run.
        branches = subprocess.run(["git", "-C", str(self.upstream), "branch"], capture_output=True, text=True)
        self.assertEqual(branches.stdout.split(), ["*", "master"])

    def test_metadata_changed_after_review_is_refused(self):
        publication = self.publication()
        xml = next(publication.glob("*.xml"))
        xml.write_bytes(xml.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "changed after review"):
            self.dry_run(publication)

    def test_downgrade_is_refused(self):
        with self.assertRaisesRegex(ValueError, "downgrade"):
            self.dry_run(self.publication("1.36.4"))

    def test_unreachable_urls_warn_in_dry_run_and_block_when_required(self):
        self.url_tool(1)
        run("commit", "-qam", "failing url tool", cwd=self.upstream)
        publication = self.publication()
        _, report = self.dry_run(publication)
        self.assertTrue(any("not reachable" in warning for warning in report["warnings"]))
        with self.assertRaisesRegex(ValueError, "not reachable"):
            self.dry_run(publication, "strict", require_live_urls=True)

    def test_already_catalogued_release_has_no_changes(self):
        publication = self.publication()
        self.dry_run(publication)
        subprocess.run(["git", "-C", str(self.upstream), "am", "-q", str(self.root / "dry-run" / "catalogue.patch")],
                       check=True, capture_output=True)
        _, report = self.dry_run(publication, "again")
        self.assertTrue(report["no_changes"])

    def fork(self):
        fork = Path(tempfile.mkdtemp(dir=self.root)) / "fork.git"
        run("clone", "-q", "--bare", str(self.upstream), str(fork))
        return fork

    def push(self, publication, reviewed, phase="fork-test", fork="rgleason/plugins", github=None, **options):
        return catalogue.push(publication, reviewed, phase, fork, "token", github=github or FakeGitHub(),
                              push_url=str(options.pop("push_url", None) or self.fork()), url_wait=0, **options)

    def test_fork_test_pushes_branch_and_base_copy_and_opens_draft_pr_inside_fork(self):
        publication = self.publication()
        reviewed, report = self.dry_run(publication)
        fork, github = self.fork(), FakeGitHub()
        url = self.push(publication, reviewed, github=github, push_url=fork)
        self.assertEqual(url, "https://github.com/example/pull/1")
        heads = subprocess.run(["git", "-C", str(fork), "branch", "--list", "catalogue-test/*"],
                               capture_output=True, text=True).stdout.split()
        branch = "catalogue-test/" + report["branch"]
        self.assertEqual(sorted(heads), sorted([branch, branch + "-base"]))
        method, path, body = github.calls[-1]
        self.assertEqual((method, path), ("POST", "/repos/rgleason/plugins/pulls"))
        self.assertEqual((body["head"], body["base"], body["draft"]), (branch, branch + "-base", True))
        author = subprocess.run(["git", "-C", str(fork), "log", "-1", "--format=%an <%ae>", branch],
                                capture_output=True, text=True).stdout.strip()
        self.assertEqual(author, "Rick Gleason <7+rgleason@users.noreply.github.com>")

    def test_upstream_phase_opens_pr_from_fork_branch_to_opencpn(self):
        publication = self.publication()
        reviewed, report = self.dry_run(publication)
        github = FakeGitHub()
        self.push(publication, reviewed, "upstream", github=github)
        method, path, body = github.calls[-1]
        self.assertEqual((method, path), ("POST", "/repos/OpenCPN/plugins/pulls"))
        self.assertEqual((body["head"], body["base"], body["draft"]),
                         ("rgleason:" + report["branch"], "master", False))

    def test_push_requires_the_reviewed_diff(self):
        publication = self.publication()
        reviewed, _ = self.dry_run(publication)
        (self.upstream / "metadata" / "other_pi-1.0.0-debian.xml").unlink()
        old = next((self.upstream / "metadata").glob("weather_routing_pi-1.36.5-*"))
        old.unlink()
        run("commit", "-qam", "someone else changed upstream", cwd=self.upstream)
        with self.assertRaisesRegex(ValueError, "differs from the reviewed dry run"):
            self.push(publication, reviewed)

    def test_push_guards(self):
        publication = self.publication()
        reviewed, _ = self.dry_run(publication)
        with self.assertRaisesRegex(ValueError, "must not be the upstream"):
            self.push(publication, reviewed, fork="OpenCPN/plugins")
        with self.assertRaisesRegex(ValueError, "can write to OpenCPN/plugins"):
            self.push(publication, reviewed, github=FakeGitHub(upstream_write=True))
        with self.assertRaisesRegex(ValueError, "can write to OpenCPN/plugins"):
            self.push(publication, reviewed, "upstream", github=FakeGitHub(upstream_write=True))
        alpha = self.publication(channel="xweather-alpha")
        alpha_reviewed, _ = self.dry_run(alpha, "alpha")
        with self.assertRaisesRegex(ValueError, "Only the production channel"):
            self.push(alpha, alpha_reviewed)


if __name__ == "__main__":
    unittest.main()
