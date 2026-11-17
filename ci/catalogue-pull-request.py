#!/usr/bin/env python3
"""Turn a reviewed, Cloudsmith-published release into an OpenCPN/plugins PR.

The catalogue XML files come only from the reviewed publication directory
(the CircleCI artifact `reviewed-publication`), hash-checked against its
publication.json. The branch is always cut from the current OpenCPN/plugins
base branch, never from a fork that may be stale.

Phases:
  dry-run    Phase 1. Clone upstream, make the branch (remove this plugin's
             older XML for the same targets, add the new XML), run upstream's
             own PR checks and write the patch, XML files and PR text to an
             output directory kept as CircleCI artifacts. Nothing is pushed.
  fork-test  Phase 2. Rebuild the branch, require it to match the reviewed
             dry run byte for byte, push it and a copy of the upstream base to
             the fork, and open a draft PR *inside the fork*. OpenCPN/plugins
             is not touched.
  upstream   Phase 3. Rebuild and match as above, push the branch to the fork
             and open the PR fork:branch -> OpenCPN/plugins:base.

Subcommands:
  recover WORKFLOW_ID OUT           fetch reviewed-publication from the
                                    publish-reviewed job of a deploy workflow
  prepare PUBLICATION OUT           phase 1
  push PUBLICATION REVIEWED --phase fork-test|upstream --fork OWNER/plugins
"""
import argparse
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import time
from urllib.error import HTTPError
from urllib.parse import quote, urlparse
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "publication", Path(__file__).with_name("prepare-alpha-artifacts.py"))
publication = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publication)

UPSTREAM = "OpenCPN/plugins"
UPSTREAM_URL = "https://github.com/OpenCPN/plugins.git"
BASE_BRANCHES = ("master", "Beta", "Alpha", "Experimental")
TRIPLE = ("target", "target-version", "target-arch")
PINNED_SCHEMA = Path(__file__).with_name("catalogue") / "ocpn-plugin.xsd"
BOT = ("WeatherRouting catalogue dry run", "catalogue-dry-run@invalid")
SAFE_NAME = re.compile(r"[A-Za-z0-9_.+-]+")
# Phase 2 uses a fine-grained token limited to the fork. Phase 3 needs a token
# that may open a PR on OpenCPN/plugins (GitHub's fine-grained tokens cannot
# open cross-fork PRs into repositories you do not own, so this is usually a
# classic token with public_repo scope). Separate variables keep phase 2 from
# ever using the broader token.
TOKEN_VARIABLES = {"fork-test": "CATALOGUE_FORK_TOKEN", "upstream": "CATALOGUE_GITHUB_TOKEN"}


def field(root, name):
    return (root.findtext(name) or "").strip()


def version_key(version):
    return tuple(int(part) if part.isdigit() else -1 for part in re.split(r"[.]", version))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git(clone, *args, capture=True, env=None):
    result = subprocess.run(["git", "-C", str(clone), *args], env=env,
                            stdout=subprocess.PIPE if capture else None)
    if result.returncode:
        # Never echo the command line: keep failures free of anything secret.
        raise RuntimeError(f"git {args[0] if args else ''} failed (exit {result.returncode}); see the git output above")
    return result.stdout if capture else b""


def push_environment(token):
    """Environment for an authenticated push to github.com over HTTPS.

    The token travels only in environment variables (GIT_CONFIG_*), never on
    the command line, so it cannot appear in a process listing or an error.
    The user's and the system's git configuration are ignored: CircleCI's
    checkout rewrites https://github.com to SSH with the project's read-only
    deploy key, which would otherwise be used instead of the token.
    """
    basic = base64.b64encode(f"x-access-token:{token}".encode()).decode()
    env = {name: value for name, value in os.environ.items()
           if not name.startswith("GIT_") and name not in TOKEN_VARIABLES.values()}
    env.update({"GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
                "GIT_TERMINAL_PROMPT": "0", "GIT_CONFIG_COUNT": "1",
                "GIT_CONFIG_KEY_0": "http.https://github.com/.extraheader",
                "GIT_CONFIG_VALUE_0": f"AUTHORIZATION: basic {basic}"})
    return env


# --------------------------------------------------------------------------
# Reviewed publication


def load_publication(directory):
    """Return the catalogue XML of a reviewed publication, hash-checked."""
    directory = Path(directory).resolve()
    manifest = json.loads((directory / "publication.json").read_text())
    channel = manifest["channel"]
    profile = publication.profile_for(channel)
    if (manifest["repository"], manifest["package"]) != (profile["repository"], profile["package"]):
        raise ValueError("Publication identity/destination differs from its channel profile")
    package = profile["package"]
    entries = [item for item in manifest["uploads"]
               if item.get("catalogue") and item["name"].endswith("-metadata")]
    if len(entries) != len(publication.TARGETS):
        raise ValueError(f"Expected {len(publication.TARGETS)} catalogue XML files, found {len(entries)}")
    files, versions, cloud_versions = [], set(), set()
    prefix = f"https://dl.cloudsmith.io/public/{profile['repository']}/raw/names/"
    for item in entries:
        # Catalogue XML is staged at the publication root; only the
        # supplemental (non-catalogue) Windows ABI lives in a subdirectory.
        path = directory / PurePosixPath(item["file"].replace("\\", "/")).name
        data = path.read_bytes()
        if sha256(data) != item["sha256"]:
            raise ValueError(f"Catalogue XML changed after review: {path.name}")
        root = ET.fromstring(data)
        if root.tag != "plugin" or field(root, "name") != profile["name"]:
            raise ValueError(f"Unexpected plugin identity in {path.name}")
        version = field(root, "version")
        triple = tuple(field(root, key) for key in TRIPLE)
        url = field(root, "tarball-url")
        if not url.startswith(prefix) or "--" in url or urlparse(url).hostname != "dl.cloudsmith.io":
            raise ValueError(f"tarball-url does not point at {profile['repository']}: {path.name}")
        if not item["name"].startswith(f"{package}-{version}-"):
            raise ValueError(f"Upload name/version mismatch: {item['name']}")
        # Same naming as the existing WeatherRouting catalogue entries:
        # <package>-<version>-<cloudsmith metadata name>.xml
        destination = f"{package}-{version}-{item['name']}.xml"
        if not SAFE_NAME.fullmatch(destination):
            raise ValueError(f"Unsafe catalogue file name: {destination}")
        files.append({"name": destination, "data": data, "target": triple, "source": path.name,
                      "sha256": item["sha256"], "tarball_url": url})
        versions.add(version)
        cloud_versions.add(item["version"])
    if len(versions) != 1 or len(cloud_versions) != 1:
        raise ValueError("Catalogue XML mixes versions")
    if len({entry["target"] for entry in files}) != len(files):
        raise ValueError("Duplicate catalogue target")
    cloud_version = cloud_versions.pop()
    build = re.fullmatch(r"[\d.]+\+(\d+)\.([0-9a-f]{7})", cloud_version)
    if not build:
        raise ValueError(f"Unexpected Cloudsmith version: {cloud_version}")
    return {"channel": channel, "profile": profile, "package": package, "name": profile["name"],
            "version": versions.pop(), "cloud_version": cloud_version, "build": build[1],
            "source_repository": manifest.get("source_repository", ""),
            "source_revision": manifest.get("source_revision", ""),
            "files": sorted(files, key=lambda entry: entry["name"])}


# --------------------------------------------------------------------------
# Branch construction and upstream checks


def check_urls(clone, paths, wait):
    """Run upstream's tools/check-metadata-urls, retrying while Cloudsmith syncs.

    Upstream code runs with a minimal environment so no token can leak to it.
    """
    tool = clone / "tools" / "check-metadata-urls"
    if not tool.is_file():
        return False, "tools/check-metadata-urls not found upstream"
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": tempfile.gettempdir(),
           "LANG": "C.UTF-8"}
    for name in ("HTTPS_PROXY", "https_proxy", "SSL_CERT_FILE", "REQUESTS_CA_BUNDLE"):
        if name in os.environ:
            env[name] = os.environ[name]
    deadline = time.monotonic() + wait
    while True:
        result = subprocess.run([sys.executable, "-I", str(tool), *map(str, paths)], cwd=clone, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode == 0:
            return True, result.stdout.strip()
        if time.monotonic() >= deadline:
            return False, result.stdout.strip()
        print("Catalogue URLs not reachable yet (Cloudsmith sync); retrying in 30 s", flush=True)
        time.sleep(30)


def schema_check(clone, paths):
    log = []
    for schema in ("ocpn-plugin.xsd", "ocpn-plugins.xsd"):
        # pre-commit uses ocpn-plugin.xsd; upstream CI_Pull uses ocpn-plugins.xsd.
        if not (clone / schema).is_file():
            continue
        for path in paths:
            result = subprocess.run(["xmllint", "--noout", "--schema", schema, str(path)], cwd=clone,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            log.append(result.stdout.strip())
            if result.returncode:
                raise ValueError(f"Upstream {schema} rejects {path.name}:\n{result.stdout}")
    if not log:
        raise ValueError("Upstream base has no catalogue schema")
    return log


def build_branch(release, workdir, base, upstream_url, url_wait, require_live_urls,
                 branch_prefix="", identity=BOT):
    if base not in BASE_BRANCHES:
        raise ValueError(f"Base branch must be one of {BASE_BRANCHES}")
    clone = Path(workdir) / "plugins"
    subprocess.run(["git", "clone", "--quiet", "--single-branch", "--branch", base, upstream_url,
                    str(clone)], check=True)
    base_sha = git(clone, "rev-parse", "HEAD").decode().strip()
    branch = f"{branch_prefix}{release['package']}-{release['version']}-build{release['build']}"
    git(clone, "checkout", "--quiet", "-b", branch)
    metadata = clone / "metadata"
    if not metadata.is_dir():
        raise ValueError("Upstream base has no metadata directory")

    new_targets = {entry["target"] for entry in release["files"]}
    new_version = version_key(release["version"])
    removed, untouched = [], []
    for path in sorted(metadata.glob("*.xml")):
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        if field(root, "name") != release["name"]:
            continue
        target, old = tuple(field(root, key) for key in TRIPLE), field(root, "version")
        if target not in new_targets:
            untouched.append(path.name)   # e.g. a platform this release no longer builds
            continue
        if not path.name.startswith(release["package"] + "-"):
            raise ValueError(f"{release['name']} entry with unexpected file name; refusing to delete {path.name}")
        if version_key(old) > new_version:
            raise ValueError(f"Refusing downgrade: catalogue has {old} for {'/'.join(target)}")
        if version_key(old) == new_version:
            # The same plugin version from a different build (other tarball
            # URLs) must never replace what users already install. Only a
            # byte-identical file, i.e. this very release already merged, is
            # accepted, and it produces no change.
            same = next((e for e in release["files"] if e["name"] == path.name), None)
            if same is None or same["data"] != path.read_bytes():
                raise ValueError(f"Version {old} is already in the catalogue for {'/'.join(target)} "
                                 f"({path.name}) from another build; bump the version")
            continue
        removed.append({"file": path.name, "version": old, "target": list(target)})
        git(clone, "rm", "--quiet", "--", f"metadata/{path.name}")
    added = []
    metadata.mkdir(exist_ok=True)    # git rm drops a directory it empties
    for entry in release["files"]:
        destination = metadata / entry["name"]
        destination.write_bytes(entry["data"])
        added.append(destination)
        git(clone, "add", "--", f"metadata/{entry['name']}")

    staged = git(clone, "diff", "--cached", "--name-status").decode().split("\n")
    staged = [line for line in staged if line]
    for line in staged:
        status, *names = line.split("\t")
        if any(not name.startswith("metadata/") for name in names):
            raise ValueError(f"Unexpected change outside metadata/: {line}")
    info = {"upstream": upstream_url, "base": base, "base_sha": base_sha, "branch": branch,
            "added": [entry["name"] for entry in release["files"]], "removed": removed,
            "untouched_same_plugin": untouched, "changes": staged, "no_changes": not staged,
            "warnings": [], "checks": {}}
    if not staged:
        info["warnings"].append("Upstream already contains exactly these files; nothing to publish")
        return clone, info

    info["checks"]["schema"] = schema_check(clone, added)
    whitespace = subprocess.run(["git", "-C", str(clone), "diff", "--cached", "--check"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if whitespace.returncode:
        raise ValueError("Whitespace errors (upstream pre-commit check):\n" + whitespace.stdout)
    info["checks"]["whitespace"] = "clean"
    live, url_log = check_urls(clone, added, url_wait)
    info["checks"]["urls"] = url_log
    if not live:
        if require_live_urls:
            raise ValueError("Catalogue URLs are not reachable:\n" + url_log)
        info["warnings"].append("Tarball/info URLs not reachable yet; phases 2 and 3 require them")
    upstream_schema = clone / "ocpn-plugin.xsd"
    if upstream_schema.is_file() and PINNED_SCHEMA.is_file() and \
            upstream_schema.read_bytes() != PINNED_SCHEMA.read_bytes():
        info["warnings"].append("ci/catalogue/ocpn-plugin.xsd differs from upstream; consider re-pinning")

    message = (f"{release['name']} {release['version']}: update catalogue metadata\n\n"
               f"Adds {len(added)} metadata files and removes {len(removed)} older "
               f"{release['name']} entries for the same targets.\n")
    name, email = identity
    git(clone, "-c", f"user.name={name}", "-c", f"user.email={email}",
        "commit", "--quiet", "--no-verify", "-m", message, capture=False)
    diff = git(clone, "diff", "--binary", "--full-index", base_sha, "HEAD")
    info["diff_sha256"] = sha256(diff)
    info["diff"] = diff
    return clone, info


def pull_request_text(release, info):
    title = f"{release['name']} {release['version']}"
    targets = "\n".join(f"- `{'/'.join(entry['target'])}` — `{entry['name']}`" for entry in release["files"])
    removed = "\n".join(f"- `{item['file']}` ({item['version']})" for item in info["removed"]) or "- none"
    build = os.environ.get("CIRCLE_BUILD_URL", "")
    body = f"""{release['name']} {release['version']} catalogue update.

Packages: Cloudsmith `{release['profile']['repository']}`, version `{release['cloud_version']}`.
Source: {release['source_repository']} @ `{release['source_revision']}`.

### Added
{targets}

### Removed (older {release['name']} for the same targets)
{removed}

### Checks run before opening this PR
- xmllint against upstream `ocpn-plugin.xsd` and `ocpn-plugins.xsd`
- upstream `tools/check-metadata-urls`
- `git diff --check`
- branch is identical to the reviewed dry run (diff sha256 `{info.get('diff_sha256', '')}`)
{('CircleCI: ' + build) if build else ''}
"""
    return title, body


def write_outputs(release, clone, info, output, phase="dry-run"):
    output.mkdir(parents=True, exist_ok=True)
    (output / "metadata").mkdir(exist_ok=True)
    for entry in release["files"]:
        (output / "metadata" / entry["name"]).write_bytes(entry["data"])
    if not info["no_changes"]:
        (output / "catalogue.diff").write_bytes(info["diff"])
        (output / "catalogue.patch").write_bytes(git(clone, "format-patch", "-1", "--stdout", "HEAD"))
    title, body = pull_request_text(release, info)
    (output / "pull-request.md").write_text(f"# {title}\n\n{body}")
    report = {key: value for key, value in info.items() if key != "diff"}
    report.update({"phase": phase, "channel": release["channel"], "plugin": release["name"],
                   "version": release["version"], "cloud_version": release["cloud_version"],
                   "title": title})
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = [f"Phase: {phase}", f"Branch: {info['branch']} from {info['upstream']} {info['base']}@{info['base_sha'][:12]}",
             f"Add {len(info['added'])}, remove {len(info['removed'])}"]
    lines += [f"  - {item['file']}" for item in info["removed"]]
    lines += [f"  + {name}" for name in info["added"]]
    lines += [f"WARNING: {text}" for text in info["warnings"]]
    (output / "SUMMARY.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines), flush=True)


def prepare(publication_dir, output, base, upstream_url, url_wait, require_live_urls):
    release = load_publication(publication_dir)
    if Path(output).exists():
        raise ValueError(f"Output already exists: {output}")
    with tempfile.TemporaryDirectory() as workdir:
        clone, info = build_branch(release, workdir, base, upstream_url, url_wait, require_live_urls)
        write_outputs(release, clone, info, Path(output))
    if release["channel"] != "production":
        print("NOTE: only the production channel may continue to phases 2 and 3")
    print("Dry run complete; nothing was pushed.")


# --------------------------------------------------------------------------
# GitHub


class GitHub:
    def __init__(self, token, api="https://api.github.com"):
        self.token, self.api = token, api

    def call(self, method, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        request = Request(self.api + path, data=data, method=method, headers={
            "Authorization": f"Bearer {self.token}", "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "weather-routing-catalogue-pr"})
        try:
            with urlopen(request, timeout=30) as response:
                return json.load(response)
        except HTTPError as error:
            detail = error.read().decode(errors="replace")[:500]
            raise RuntimeError(f"GitHub {method} {path}: {error.code} {detail}") from None


def check_access(github, fork, upstream):
    user = github.call("GET", "/user")
    repo = github.call("GET", f"/repos/{fork}")
    parent = (repo.get("parent") or {}).get("full_name", "")
    if not repo.get("fork") or parent.lower() != upstream.lower():
        raise ValueError(f"{fork} is not a fork of {upstream}")
    if not repo.get("permissions", {}).get("push"):
        raise ValueError(f"Token cannot push to {fork}")
    # The PR only needs to be opened; upstream's checks and maintainers merge it.
    # A token that could push to the catalogue itself is more than required.
    if github.call("GET", f"/repos/{upstream}").get("permissions", {}).get("push", False):
        raise ValueError(f"Token can write to {upstream}; use one that can only push to {fork} and open PRs")
    return user


def push(publication_dir, reviewed, phase, fork, token, upstream=UPSTREAM, draft=False,
         url_wait=600, github=None, push_url=None):
    if phase not in ("fork-test", "upstream"):
        raise ValueError("push phase must be fork-test or upstream")
    approved = json.loads((Path(reviewed) / "report.json").read_text())
    if approved.get("phase") != "dry-run":
        raise ValueError("Reviewed directory is not a phase 1 dry run")
    if approved["no_changes"]:
        print("Reviewed dry run had no changes; nothing to push")
        return None
    release = load_publication(publication_dir)
    if release["channel"] != "production":
        raise ValueError("Only the production channel is published to the OpenCPN catalogue")
    if not re.fullmatch(r"[A-Za-z0-9-]+/[A-Za-z0-9._-]+", fork) or fork.lower() == upstream.lower():
        raise ValueError("--fork must be OWNER/REPO and must not be the upstream repository")
    if not token:
        raise ValueError(f"{TOKEN_VARIABLES[phase]} is not set (CircleCI context opencpn-plugins-pr)")
    github = github or GitHub(token)
    user = check_access(github, fork, upstream)
    identity = (user.get("name") or user["login"], f"{user['id']}+{user['login']}@users.noreply.github.com")
    prefix = "catalogue-test/" if phase == "fork-test" else ""
    with tempfile.TemporaryDirectory() as workdir:
        clone, info = build_branch(release, workdir, approved["base"], approved["upstream"], url_wait,
                                   True, prefix, identity)
        if info.get("diff_sha256") != approved["diff_sha256"]:
            raise ValueError("Rebuilt branch differs from the reviewed dry run "
                             "(upstream moved or the publication changed); re-run phase 1 and review again")
        remote = push_url or f"https://github.com/{fork}.git"
        env = push_environment(token)
        owner = fork.split("/")[0]
        title, body = pull_request_text(release, info)
        if phase == "fork-test":
            base_ref = info["branch"] + "-base"
            refs = [f"{info['base_sha']}:refs/heads/{base_ref}", f"HEAD:refs/heads/{info['branch']}"]
            git(clone, "push", "--quiet", remote, *refs, capture=False, env=env)
            target_repo, head, base, is_draft = fork, info["branch"], base_ref, True
            body = ("**Phase 2 test PR inside the fork; not sent to OpenCPN/plugins.** "
                    f"Base `{base_ref}` is a copy of {upstream}@`{info['base_sha'][:12]}`.\n\n" + body)
        else:
            git(clone, "push", "--quiet", remote, f"HEAD:refs/heads/{info['branch']}", capture=False, env=env)
            target_repo, head, base, is_draft = upstream, f"{owner}:{info['branch']}", info["base"], draft
        existing = github.call("GET", f"/repos/{target_repo}/pulls?state=open&head={quote(owner + ':' + info['branch'])}")
        if existing:
            pr = existing[0]
            print(f"PR already open: {pr['html_url']}")
        else:
            pr = github.call("POST", f"/repos/{target_repo}/pulls", {
                "title": title, "head": head, "base": base, "body": body,
                "draft": is_draft, "maintainer_can_modify": True})
            print(f"Opened {'draft ' if is_draft else ''}PR: {pr['html_url']}")
        out = Path(reviewed).parent / f"catalogue-pr-{phase}"
        write_outputs(release, clone, info, out, phase)
        (out / "pull-request-url.txt").write_text(pr["html_url"] + "\n")
        return pr["html_url"]


# --------------------------------------------------------------------------
# Recover the reviewed publication from an earlier deploy workflow


def fetch_json(url):
    with urlopen(url, timeout=30) as response:
        return json.load(response)


def recover(workflow_id, output, job_name="publish-reviewed"):
    if not re.fullmatch(r"[0-9a-fA-F-]{36}", workflow_id):
        raise ValueError("Expected a CircleCI workflow UUID")
    jobs = fetch_json(f"https://circleci.com/api/v2/workflow/{workflow_id}/job")["items"]
    matches = [job for job in jobs if job["name"] == job_name]
    if len(matches) != 1 or matches[0]["status"] != "success":
        raise ValueError(f"{job_name} must have succeeded in workflow {workflow_id}")
    job = matches[0]
    artifacts = fetch_json(f"https://circleci.com/api/v1.1/project/"
                           f"{quote(job['project_slug'], safe='/')}/{job['job_number']}/artifacts")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    count = 0
    for artifact in artifacts:
        path = PurePosixPath(artifact["path"])
        if not path.parts or path.parts[0] != "reviewed-publication" or ".." in path.parts:
            continue
        if urlparse(artifact["url"]).hostname != "output.circle-artifacts.com":
            raise ValueError("Unexpected artifact host")
        destination = output.joinpath(*path.parts[1:])
        destination.parent.mkdir(parents=True, exist_ok=True)
        with urlopen(artifact["url"], timeout=60) as response, destination.open("wb") as stream:
            shutil.copyfileobj(response, stream)
        count += 1
    if not (output / "publication.json").is_file():
        raise ValueError("Recovered artifacts contain no publication.json")
    load_publication(output)    # hash-checks every catalogue XML
    print(f"Recovered {count} reviewed-publication files from {job_name} #{job['job_number']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    rec = sub.add_parser("recover")
    rec.add_argument("workflow_id")
    rec.add_argument("output", type=Path)
    pre = sub.add_parser("prepare")
    pre.add_argument("publication", type=Path)
    pre.add_argument("output", type=Path)
    pre.add_argument("--base", default="master", choices=BASE_BRANCHES)
    pre.add_argument("--upstream-url", default=UPSTREAM_URL)
    pre.add_argument("--url-wait", type=int, default=300)
    pre.add_argument("--require-live-urls", action="store_true")
    psh = sub.add_parser("push")
    psh.add_argument("publication", type=Path)
    psh.add_argument("reviewed", type=Path)
    psh.add_argument("--phase", required=True, choices=("fork-test", "upstream"))
    psh.add_argument("--fork", required=True)
    psh.add_argument("--draft", action="store_true")
    psh.add_argument("--url-wait", type=int, default=600)
    args = parser.parse_args()
    if args.command == "recover":
        recover(args.workflow_id, args.output)
    elif args.command == "prepare":
        prepare(args.publication, args.output, args.base, args.upstream_url, args.url_wait,
                args.require_live_urls)
    else:
        push(args.publication, args.reviewed, args.phase, args.fork,
             os.environ.get(TOKEN_VARIABLES[args.phase], ""), draft=args.draft,
             url_wait=args.url_wait)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
