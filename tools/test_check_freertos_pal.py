#!/usr/bin/env python3
"""Offline regression tests for the FreeRTOS header cache."""

from contextlib import contextmanager
import importlib.util
import io
import multiprocessing
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import traceback
import unittest
from unittest import mock


sys.dont_write_bytecode = True

SCRIPT = Path(__file__).with_name("check_freertos_pal.py")
VERSION = "V11.1.0"
COMMIT = "dbf70559b27d39c1fdb68dfb9a32140b6a6777a0"
HEADERS = {"FreeRTOS.h": b"/* kernel */\n", "task.h": b"/* tasks */\n",
           "semphr.h": b"/* semaphores */\n"}


def load_checker():
    spec = importlib.util.spec_from_file_location("check_freertos_pal", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def header_archive(headers=HEADERS):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w:gz") as archive:
        for name, data in headers.items():
            entry = tarfile.TarInfo(f"FreeRTOS-Kernel-{COMMIT}/include/{name}")
            entry.size = len(data)
            archive.addfile(entry, io.BytesIO(data))
    return output.getvalue()


@contextmanager
def working_directory(path):
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def concurrent_writer(cache, archive, barrier, results):
    """Force two real processes to finish extraction before either publishes."""
    checker = load_checker()
    extract = checker.extract_headers

    def synchronized_extract(data, target):
        extract(data, target)
        barrier.wait(timeout=15)

    try:
        with mock.patch.object(checker, "download_archive", return_value=archive), \
                mock.patch.object(checker, "extract_headers",
                                  side_effect=synchronized_extract):
            headers = checker.ensure_headers(VERSION, COMMIT, Path(cache))
            results.put(("ok", str(headers), (headers / "FreeRTOS.h").read_bytes()))
    except Exception:
        results.put(("error", traceback.format_exc()))


class HeaderCacheTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="freertos-cache-test-")
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name).resolve()
        self.cache = self.work / "cache"
        self.checker = load_checker()

    def populate(self):
        with mock.patch.object(self.checker, "download_archive",
                               return_value=header_archive()):
            return self.checker.ensure_headers(VERSION, COMMIT, self.cache)

    def assert_complete(self, headers):
        self.assertEqual((headers / ".pinned-commit").read_text().strip(), COMMIT)
        for name, contents in HEADERS.items():
            self.assertEqual((headers / name).read_bytes(), contents)

    def test_relative_cache_produces_valid_wrapper_symlink(self):
        for cold in (True, False):
            with self.subTest(cold_cache=cold), working_directory(self.work), \
                    mock.patch.object(self.checker, "download_archive",
                                      return_value=header_archive()) as download:
                headers = self.checker.ensure_headers(VERSION, COMMIT, Path("cache"))
                wrapper = self.work / f"wrapper-{cold}" / "freertos"
                wrapper.parent.mkdir()
                wrapper.symlink_to(headers, target_is_directory=True)
                self.assert_complete(wrapper)
                self.assertTrue(headers.is_absolute())
                self.assertEqual(download.call_count, int(cold))

    def test_cache_hit_preserves_headers_without_downloading(self):
        headers = self.populate()
        before = {name: (headers / name).stat().st_ino for name in HEADERS}
        with mock.patch.object(self.checker, "download_archive",
                               side_effect=AssertionError("cache hit used the network")):
            self.assertEqual(self.checker.ensure_headers(VERSION, COMMIT, self.cache),
                             headers)
        self.assert_complete(headers)
        self.assertEqual(before, {name: (headers / name).stat().st_ino for name in HEADERS})

    def test_concurrent_cold_writers_publish_one_complete_cache(self):
        self.cache.mkdir()
        context = multiprocessing.get_context("spawn")
        barrier = context.Barrier(2)
        results = context.Queue()
        processes = [context.Process(target=concurrent_writer,
                                     args=(str(self.cache), header_archive(), barrier, results))
                     for _ in range(2)]
        try:
            for process in processes:
                process.start()
            outcomes = [results.get(timeout=25) for _ in processes]
            for process in processes:
                process.join(timeout=5)
                self.assertEqual(process.exitcode, 0)
            self.assertEqual([outcome[0] for outcome in outcomes], ["ok", "ok"], outcomes)
            self.assertEqual(outcomes[0][1:], outcomes[1][1:])
            self.assert_complete(Path(outcomes[0][1]))
            self.assertEqual(list(self.cache.iterdir()), [Path(outcomes[0][1])])
        finally:
            for process in processes:
                if process.is_alive():
                    process.terminate()
                if process.pid is not None:
                    process.join(timeout=5)
            results.close()
            results.join_thread()

    def test_competing_publish_between_cache_checks_is_reused(self):
        self.cache.mkdir()
        headers = self.cache / f"{VERSION}-{COMMIT}"
        exists = Path.exists

        def publish_before_exists(path):
            if path == headers:
                self.checker.extract_headers(header_archive(), headers)
                (headers / ".pinned-commit").write_text(COMMIT + "\n")
            return exists(path)

        with mock.patch.object(Path, "exists", new=publish_before_exists), \
                mock.patch.object(self.checker, "download_archive",
                                  side_effect=AssertionError("winner was not reused")):
            self.assertEqual(self.checker.ensure_headers(VERSION, COMMIT, self.cache),
                             headers)
        self.assert_complete(headers)

    def test_failed_extraction_is_cleaned_and_can_be_retried(self):
        partial = header_archive({"FreeRTOS.h": HEADERS["FreeRTOS.h"]})
        with mock.patch.object(self.checker, "download_archive", return_value=partial):
            with self.assertRaisesRegex(RuntimeError, "missing task.h"):
                self.checker.ensure_headers(VERSION, COMMIT, self.cache)
        self.assertEqual(list(self.cache.iterdir()), [])
        self.assert_complete(self.populate())

    def test_incomplete_published_cache_is_not_deleted(self):
        headers = self.populate()
        (headers / "task.h").unlink()
        preserved = headers / "FreeRTOS.h"
        inode = preserved.stat().st_ino
        with mock.patch.object(self.checker, "download_archive",
                               side_effect=AssertionError("invalid cache used the network")):
            with self.assertRaisesRegex(RuntimeError, "Incomplete header cache"):
                self.checker.ensure_headers(VERSION, COMMIT, self.cache)
        self.assertEqual(preserved.stat().st_ino, inode)
        self.assertEqual(preserved.read_bytes(), HEADERS["FreeRTOS.h"])


if __name__ == "__main__":
    unittest.main()
