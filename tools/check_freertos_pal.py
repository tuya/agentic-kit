#!/usr/bin/env python3
"""Compile the PAL with pinned upstream FreeRTOS headers (no kernel execution)."""

import argparse
import io
import os
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.request


KERNELS = (
    ("V10.4.3", "9c048e0c71ee43630394981a86f5265bc57131e4", 10, (16, 32)),
    ("V11.1.0", "dbf70559b27d39c1fdb68dfb9a32140b6a6777a0", 11, (16, 32, 64)),
)

DEFAULT_CACHE = Path.home() / ".cache" / "agentic-kit" / "freertos-kernels"


def download_archive(commit, attempts=3):
    """Fetch the pinned kernel tarball, retrying transient network failures."""
    url = f"https://codeload.github.com/FreeRTOS/FreeRTOS-Kernel/tar.gz/{commit}"
    last_error = None
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                return response.read()
        except (urllib.error.URLError, OSError) as exc:
            last_error = exc
            if attempt < attempts:
                print(f"Download attempt {attempt}/{attempts} failed ({exc}); retrying",
                      flush=True)
                time.sleep(3)
    raise RuntimeError(f"Could not download {commit} after {attempts} attempts: {last_error}")


def extract_headers(archive_bytes, target):
    target.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(archive_bytes), mode="r:gz") as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts
            # Copy only direct include/*.h files; never extract archive paths.
            if (member.isfile() and len(parts) == 3 and parts[1] == "include"
                    and parts[2].endswith(".h")):
                with archive.extractfile(member) as source:
                    (target / parts[2]).write_bytes(source.read())
    for header in ("FreeRTOS.h", "task.h", "semphr.h"):
        if not (target / header).is_file():
            raise RuntimeError(f"Upstream archive is missing {header}")


def ensure_headers(version, commit, cache_dir):
    """Materialize the pinned headers under cache_dir and return their directory.

    The cache is keyed by version+commit, so a hit can never serve another
    revision. Each writer owns its staging directory and publishes it atomically;
    a concurrent winner is reused without modifying its published headers."""
    cache_dir = Path(cache_dir).resolve()
    headers = cache_dir / f"{version}-{commit}"
    stamp = headers / ".pinned-commit"

    def cache_ready():
        return (stamp.is_file() and stamp.read_text().strip() == commit
                and all((headers / name).is_file()
                        for name in ("FreeRTOS.h", "task.h", "semphr.h")))

    if cache_ready():
        print(f"Using cached {version} headers at {commit}", flush=True)
        return headers
    if headers.exists():
        # A writer can finish between the readiness check and exists().
        if cache_ready():
            print(f"Using cached {version} headers at {commit}", flush=True)
            return headers
        raise RuntimeError(f"Incomplete header cache at {headers}; remove it and retry")
    print(f"Downloading {version} headers at {commit}", flush=True)
    cache_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
            prefix=f".staging-{version}-{commit}-", dir=cache_dir) as directory:
        staging = Path(directory) / "include"
        extract_headers(download_archive(commit), staging)
        (staging / ".pinned-commit").write_text(commit + "\n")
        try:
            staging.rename(headers)
        except OSError:
            # Another process may have published after our initial cache check.
            # Never remove its directory: its compiler may already be using it.
            if not cache_ready():
                raise
    return headers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"),
                        help="C compiler command (default: CC or cc)")
    parser.add_argument("--kernels-dir", type=Path,
                        help="Offline directory with pinned V10.4.3/V11.1.0 git checkouts")
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE,
                        help=f"Download cache, reused across runs (default: {DEFAULT_CACHE})")
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    if not compiler:
        parser.error("--cc must name a compiler")
    repo = Path(__file__).resolve().parent.parent
    probe = repo / "pal/test/freertos_compat"
    passed = 0
    with tempfile.TemporaryDirectory(prefix="agentic-kit-freertos-") as directory:
        work = Path(directory)
        for version, commit, major, widths in KERNELS:
            if args.kernels_dir:
                kernel = args.kernels_dir.resolve() / version
                actual = subprocess.check_output(
                    ["git", "-C", str(kernel), "rev-parse", "HEAD"], text=True).strip()
                if actual != commit:
                    raise RuntimeError(f"{version}: expected {commit}, got {actual}")
                dirty = subprocess.check_output(
                    ["git", "-C", str(kernel), "status", "--porcelain", "--", "include"],
                    text=True).strip()
                if dirty:
                    raise RuntimeError(f"{version}: local headers have changes")
                headers = kernel / "include"
            else:
                headers = ensure_headers(version, commit, args.cache_dir)
            wrapper = work / version / "wrapper"
            wrapper.mkdir(parents=True)
            (wrapper / "freertos").symlink_to(headers, target_is_directory=True)
            for bits in widths:
                command = compiler + [
                    "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                    f"-DTEST_KERNEL_MAJOR={major}", f"-DTEST_TICK_BITS={bits}",
                    "-DAGENTIC_KIT_LOG_LEVEL=0",
                    # Strict C99 hides POSIX declarations in glibc's netdb.h
                    # (struct addrinfo, getaddrinfo); the shim passes through to
                    # the host headers, so request POSIX.1-2001 explicitly.
                    # Defining _POSIX_C_SOURCE alone would hide BSD socket flags
                    # on darwin (MSG_DONTWAIT); _DARWIN_C_SOURCE restores them
                    # and is inert on glibc.
                    "-D_POSIX_C_SOURCE=200112L", "-D_DARWIN_C_SOURCE",
                ]
                command += [f"-I{path}" for path in (
                    wrapper, headers, probe, repo / "pal/test", repo / "pal", repo / "common"
                )]
                command.append(str(repo / "pal/pal_freertos.c"))
                subprocess.run(command, check=True)
                passed += 1
                print(f"PASS: {version} ({commit}), {bits}-bit ticks", flush=True)
    print(f"FreeRTOS C99 header compatibility: {passed}/5; no kernel runtime tested.")


if __name__ == "__main__":
    main()
