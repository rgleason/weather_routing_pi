#!/usr/bin/env python3
"""Seed Homebrew's gettext source cache from bounded, checksum-checked mirrors.

The existing macOS gettext smoke test can require a source rebuild. GNU's
default download hosts occasionally time out on CI runners. Use the checksum
from the runner's own Homebrew formula, never accept an unchecked substitute.
GNU mirror list: https://www.gnu.org/prep/ftp.html
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile
from urllib.parse import urlparse


MIRRORS = (
    "https://mirrors.ocf.berkeley.edu/gnu/gettext/",
    "https://mirror.csclub.uwaterloo.ca/gnu/gettext/",
    "https://mirrors.kernel.org/gnu/gettext/",
)


def download(url, destination):
    subprocess.run([
        "curl", "--fail", "--location", "--proto", "=https",
        "--proto-redir", "=https", "--connect-timeout", "10",
        "--max-time", "90", "--retry", "1", "--output",
        str(destination), url,
    ], check=True)


def ensure_source(formula, cache, fetcher=download):
    stable = formula["urls"]["stable"]
    url = urlparse(stable["url"])
    filename = PurePosixPath(url.path).name
    checksum = stable["checksum"]
    if (url.scheme != "https" or url.hostname not in ("ftp.gnu.org", "ftpmirror.gnu.org")
            or not re.fullmatch(r"gettext-[0-9][A-Za-z0-9_.-]*\.tar\.(gz|xz|bz2|lz)", filename)
            or not re.fullmatch(r"[0-9a-f]{64}", checksum)):
        raise ValueError("Unexpected gettext source or checksum in Homebrew formula")
    cache = Path(cache)
    if not cache.is_absolute():
        raise ValueError("Expected an absolute Homebrew source-cache path")
    if cache.is_file() and hashlib.sha256(cache.read_bytes()).hexdigest() == checksum:
        print("Verified existing gettext source cache", flush=True)
        return
    cache.parent.mkdir(parents=True, exist_ok=True)
    for mirror in MIRRORS:
        with tempfile.NamedTemporaryFile(dir=cache.parent, prefix=cache.name + ".",
                                         suffix=".partial", delete=False) as stream:
            temporary = Path(stream.name)
        try:
            print("Fetching gettext source from " + mirror + filename, flush=True)
            fetcher(mirror + filename, temporary)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != checksum:
                print("Rejected gettext source: checksum mismatch", flush=True)
                continue
            temporary.replace(cache)
            print("Verified gettext source against Homebrew SHA-256", flush=True)
            return
        except (OSError, subprocess.CalledProcessError) as error:
            print("gettext mirror unavailable: " + str(error), flush=True)
        finally:
            temporary.unlink(missing_ok=True)
    raise RuntimeError("No GNU mirror supplied the checksum-verified gettext source")


def main():
    info = json.loads(subprocess.check_output(
        ["brew", "info", "--json=v2", "gettext"], text=True))
    formula, = info["formulae"]
    cache = subprocess.check_output(
        ["brew", "--cache", "--build-from-source", "gettext"], text=True).strip()
    if not cache or "\n" in cache:
        raise ValueError("Expected one Homebrew gettext source-cache path")
    ensure_source(formula, cache)


if __name__ == "__main__":
    main()
