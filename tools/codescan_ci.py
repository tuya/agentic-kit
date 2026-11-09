#!/usr/bin/env python3
"""CodeScan security-scan integration for GitLab CI.

Implements the CodeScan CI API contract (trigger -> poll -> details): the CI
side never uploads source -- the backend clones repo_url@commit_sha itself --
so this script only triggers the scan, polls until the task settles, pulls
the vulnerability list for the scanned version and applies the blocking
decision.

Environment (all optional except the two marked required):

  CODESCAN_API_BASE    (required) e.g. https://codescan.example.com
  CODESCAN_TOKEN       (required) Bearer token; store as a Masked CI variable
  SCAN_TYPE            quick / codeql / agent; empty = platform default
  FULL_SCAN            true = force full scan (first scan is always full)
  BLOCK_SEVERITIES     comma/space separated list; empty = platform's
                       block_severities ("platform changes config, every
                       repo follows")
  BLOCK_ON_VULN        false/0 = report only, never fail on findings
  SCAN_TIMEOUT_SEC     polling budget, default 3600 (agent scans: >= 7200)
  SCAN_POLL_INTERVAL   poll cadence in seconds, default 10

GitLab predefined variables are picked up automatically (CI_PROJECT_PATH,
CI_REPOSITORY_URL, CI_COMMIT_REF_NAME, CI_COMMIT_SHA, GITLAB_USER_NAME,
CI_MERGE_REQUEST_IID). Credentials embedded in CI_REPOSITORY_URL are stripped
before the URL leaves the runner: the backend injects its own clone token and
the CI job token has no business reaching the scan platform.

Exit code 0 = passed (or BLOCK_ON_VULN=false); non-zero = trigger/poll
failure, timeout, or a blocking severity was found. A JSON report is written
to CODESCAN_REPORT (default codescan-report.json) on every settled run, also
on failure, for the job artifact.
"""

import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

TRUTHY_VALUES = ("1", "true", "yes", "on")
SEVERITY_RANK = {"critical": 0, "high": 1, "medium": 2, "low": 3, "info": 4}
REQUEST_TIMEOUT = 30
# Transient-network tolerance while polling: single failed queries must be
# retried (platform guidance), so only a burst of consecutive failures -- not
# any one error -- gives up. 10 misses at the default cadence is ~100 s.
MAX_CONSECUTIVE_POLL_ERRORS = 10
MAX_DETAIL_PAGES = 100
# Job-log print budget: LIST_LIMIT findings get one-liners, the first
# DETAIL_LIMIT of those also get full detail blocks (what / fix / code). The
# JSON report artifact always carries every finding in full.
LIST_LIMIT = 50
DETAIL_LIMIT = 20


def log(msg):
    print(msg, flush=True)


def env(name, default=None):
    value = os.environ.get(name)
    if value is None or not value.strip():
        return default
    return value.strip()


def truthy(name):
    value = env(name)
    return value is not None and value.lower() in TRUTHY_VALUES


def fatal(msg):
    log(f"ERROR: {msg}")
    sys.exit(1)


def strip_userinfo(url):
    """Remove user:pass@ from an http(s) URL, keeping scheme/host/port/path."""
    parts = urllib.parse.urlsplit(url)
    if parts.username is None and parts.password is None:
        return url
    host = parts.hostname or ""
    if parts.port:
        host = f"{host}:{parts.port}"
    return urllib.parse.urlunsplit(
        (parts.scheme, host, parts.path, parts.query, parts.fragment))


def http_json(method, url, token, payload=None, timeout=REQUEST_TIMEOUT):
    """One request; raises urllib.error.HTTPError (4xx/5xx) or URLError."""
    body = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Accept", "application/json")
    if body is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read()
    try:
        return json.loads(raw or b"{}")
    except ValueError:
        raise RuntimeError(f"{url} returned a non-JSON body") from None


def describe_http_error(exc):
    """HTTPError -> readable text including the API's `detail` when present."""
    detail = ""
    try:
        body = exc.read()
        try:
            parsed = json.loads(body or b"{}")
            detail = f": {parsed.get('detail') or parsed.get('message') or body.decode('utf-8', 'replace')[:300]}"
        except ValueError:
            detail = f": {body.decode('utf-8', 'replace')[:300]}"
    except Exception:
        pass
    return f"HTTP {exc.code}{detail}"


def retry_http(func, attempts, delay, what):
    """Retry on transport errors; surface HTTPError from the last attempt."""
    last = None
    for attempt in range(1, attempts + 1):
        try:
            return func()
        except urllib.error.HTTPError as exc:
            if exc.code < 500:  # 4xx is definitive: do not hammer the API
                hint = " (check CODESCAN_TOKEN)" if exc.code == 401 else ""
                fatal(f"{what} failed: {describe_http_error(exc)}{hint}")
            last = exc
        except (urllib.error.URLError, OSError, RuntimeError) as exc:
            last = exc
        if attempt < attempts:
            log(f"{what}: transient error ({last}); retry {attempt}/{attempts - 1}")
            time.sleep(delay)
    fatal(f"{what} failed after {attempts} attempts: {last}")


def parse_block_severities(raw, platform):
    """CI-side override wins; empty falls back to the platform list."""
    if raw:
        source = "CI variable BLOCK_SEVERITIES"
        values = re.split(r"[,\s]+", raw.lower())
    else:
        source = "platform block_severities"
        values = [str(s).lower() for s in (platform or [])]
    values = [v for v in values if v]
    unknown = [v for v in values if v not in SEVERITY_RANK]
    if unknown:
        log(f"WARNING: unknown severities ignored for blocking: {', '.join(unknown)}")
    return [v for v in values if v in SEVERITY_RANK], source


def zero_counts():
    return {name: 0 for name in SEVERITY_RANK}


def counts_from_items(items):
    counts = zero_counts()
    for item in items:
        severity = str(item.get("severity") or "").lower()
        if severity in counts:
            counts[severity] += 1
    return counts


def trigger_scan(base, token, payload):
    def call():
        return http_json("POST", f"{base}/api/v1/ci/scan", token, payload)

    return retry_http(call, attempts=3, delay=5, what="Scan trigger")


def poll_until_settled(base, token, task_id, timeout_sec, interval_sec):
    """GET /api/v1/ci/scan/{id} until completed/failed/stopped or deadline.

    Returns the final status document; exits the process on give-up
    conditions (deadline, unreachable task, error burst)."""
    deadline = time.monotonic() + timeout_sec
    consecutive_errors = 0
    last_error = None
    last_label = ""
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            fatal(f"Scan did not settle within SCAN_TIMEOUT_SEC={timeout_sec}s "
                  f"(task {task_id}); see the CodeScan platform for its state")
        try:
            status = http_json("GET", f"{base}/api/v1/ci/scan/{task_id}", token)
            consecutive_errors = 0
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                fatal(f"Task {task_id} disappeared from the platform "
                      f"({describe_http_error(exc)})")
            consecutive_errors += 1
            last_error = describe_http_error(exc)
        except (urllib.error.URLError, OSError, RuntimeError) as exc:
            consecutive_errors += 1
            last_error = str(exc)
        if consecutive_errors:
            if consecutive_errors >= MAX_CONSECUTIVE_POLL_ERRORS:
                fatal(f"Polling failed {consecutive_errors} times in a row "
                      f"(task {task_id}); last error: {last_error}")
            time.sleep(min(interval_sec, max(remaining, 0.5)))
            continue
        state = str(status.get("status") or "")
        label = (f"{state} {status.get('progress', 0)}% "
                 f"({status.get('scanned_files', 0)}/{status.get('total_files', 0)} files)")
        if label != last_label:
            log(f"[poll] {label}")
            last_label = label
        if state == "completed":
            return status
        if state in ("failed", "stopped"):
            fatal(f"Scan task {state}: {status.get('error_msg') or 'no error message'}")
        time.sleep(min(interval_sec, max(remaining, 0.5)))


def fetch_details(base, token, project_id, version_id):
    """Vulnerability list for this version, paginated; None on final failure."""
    def fetch_all():
        items = []
        for page in range(1, MAX_DETAIL_PAGES + 1):
            query = urllib.parse.urlencode(
                {"project_id": project_id, "version_id": version_id,
                 "page": page, "page_size": 500})
            data = http_json("GET", f"{base}/api/v1/vulnerabilities?{query}", token)
            batch = data.get("items") or []
            items.extend(batch)
            total = int(data.get("total") or 0)
            if not batch or (total and len(items) >= total):
                return items
        return items

    try:
        return retry_http(fetch_all, attempts=3, delay=5,
                          what="Vulnerability details fetch")
    except SystemExit:
        # retry_http fatals on definitive errors; fall back to severity_counts
        return None


def trim(text, limit=400):
    text = (text or "").strip()
    if len(text) <= limit:
        return text
    return text[:limit].rstrip() + " ...[trimmed]"


def trim_snippet(snippet, max_lines=15):
    lines = (snippet or "").strip().splitlines()
    if len(lines) <= max_lines:
        return "\n".join(lines)
    return "\n".join(lines[:max_lines]) + f"\n... [{len(lines) - max_lines} more lines]"


def print_findings(items, blocked_severities):
    if not items:
        log("No findings recorded for this version.")
        return
    ranked = sorted(items, key=lambda i: SEVERITY_RANK.get(
        str(i.get("severity") or "").lower(), 99))
    log(f"Findings ({len(ranked)} total; blocking marked with '*'):")
    for index, item in enumerate(ranked[:LIST_LIMIT]):
        severity = str(item.get("severity") or "?").lower()
        mark = "*" if severity in blocked_severities else " "
        flag = ""
        if item.get("is_incremental"):
            flag = " (new vs baseline)"
        if item.get("label_status") == "ignored":
            flag += " [ignored]"
        log(f" {mark}[{severity:<8}] {item.get('file_path', '?')}:{item.get('lines', '?')}"
            f" -- {item.get('title', '?')}{flag}")
        # Full detail (description / suggestion / snippet) for the top items
        # so the pipeline log itself reads as a report; the rest stay
        # one-liners and the JSON report artifact carries everything.
        if index < DETAIL_LIMIT:
            if item.get("description"):
                log(f"     what: {trim(item['description'])}")
            if item.get("suggestion"):
                log(f"     fix : {trim(item['suggestion'])}")
            if item.get("code_snippet"):
                snippet = trim_snippet(item["code_snippet"])
                log("     code:")
                for line in snippet.splitlines():
                    log(f"       | {line}")
            log("")
    if len(ranked) > LIST_LIMIT:
        log(f" ... and {len(ranked) - LIST_LIMIT} more (see the report artifacts)")


def write_report(path, report):
    try:
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(report, handle, ensure_ascii=False, indent=2)
        log(f"Report written: {path}")
    except OSError as exc:
        log(f"WARNING: could not write report {path}: {exc}")


def main():
    base = env("CODESCAN_API_BASE")
    token = env("CODESCAN_TOKEN")
    if not base or not token:
        missing = ", ".join(name for name, value in
                            (("CODESCAN_API_BASE", base), ("CODESCAN_TOKEN", token))
                            if not value)
        fatal(f"{missing} not configured. Set them as project/group CI/CD "
              f"variables (token Masked). Do not mark the token Protected "
              f"unless every scanned branch is protected -- feature branches "
              f"would lose the scan gate.")
    base = base.rstrip("/")

    project_name = env("CI_PROJECT_PATH")
    repo_url = env("CI_REPOSITORY_URL")
    branch = env("CI_COMMIT_REF_NAME")
    commit_sha = env("CI_COMMIT_SHA")
    if not all((project_name, repo_url, branch, commit_sha)):
        fatal("GitLab predefined variables missing "
              "(CI_PROJECT_PATH/CI_REPOSITORY_URL/CI_COMMIT_REF_NAME/CI_COMMIT_SHA); "
              "run inside a GitLab pipeline or export them for a local dry run")

    payload = {
        "project_name": project_name,
        "repo_url": strip_userinfo(repo_url),
        "branch": branch,
        "commit_sha": commit_sha,
        "developer_name": env("GITLAB_USER_NAME"),
        "mr_iid": env("CI_MERGE_REQUEST_IID"),
    }
    payload = {k: v for k, v in payload.items() if v is not None}
    scan_type = env("SCAN_TYPE")
    if scan_type:
        payload["scan_type"] = scan_type
    if truthy("FULL_SCAN"):
        payload["full_scan"] = True

    log("== CodeScan trigger ==")
    log(f"project : {project_name}  branch {branch}  commit {commit_sha[:12]}")
    trigger = trigger_scan(base, token, payload)
    task_id = trigger.get("task_id")
    if not task_id:
        fatal(f"Trigger response has no task_id: {json.dumps(trigger)[:300]}")
    log(f"task    : {task_id}  type {trigger.get('scan_type')}  "
        f"full_scan {str(trigger.get('full_scan')).lower()}")
    log(f"baseline: {trigger.get('previous_version_id') or '(full scan)'}")
    if trigger.get("message"):
        log(f"message : {trigger['message']}")

    timeout_sec = int(env("SCAN_TIMEOUT_SEC", "3600"))
    interval_sec = float(env("SCAN_POLL_INTERVAL", "10"))
    log(f"== Polling (every {interval_sec:g}s, budget {timeout_sec}s) ==")
    status = poll_until_settled(base, token, task_id, timeout_sec, interval_sec)

    log("== Result ==")
    items = fetch_details(base, token,
                          trigger.get("project_id"), trigger.get("version_id"))
    if items is not None:
        counts = counts_from_items(items)
        counts_source = "vulnerability details"
    else:
        raw_counts = status.get("severity_counts")
        if raw_counts is None:
            # Fail closed: "completed" with neither details nor counts means
            # the gate has nothing to judge on -- passing here would turn a
            # platform incident into a silent clean bill.
            fatal("Scan completed but the platform returned neither "
                  "vulnerability details nor severity_counts; cannot judge")
        counts = {name: int((raw_counts or {}).get(name, 0)) for name in SEVERITY_RANK}
        counts_source = "task severity_counts (details unavailable)"
    log(f"severity: {' '.join(f'{k}={v}' for k, v in counts.items())}  [{counts_source}]")

    block, block_source = parse_block_severities(
        env("BLOCK_SEVERITIES"),
        trigger.get("block_severities") or status.get("block_severities"))
    hits = [name for name in block if counts.get(name, 0) > 0]
    blocked = bool(hits)

    block_on_vuln = env("BLOCK_ON_VULN")
    report_only = (block_on_vuln is not None
                   and block_on_vuln.lower() not in TRUTHY_VALUES)
    if items is not None:
        log("== Report ==")
        print_findings(items, block)
    else:
        log("Findings list unavailable (details fetch failed); "
            "blocking judged on the task's severity_counts. "
            "See the CodeScan platform for the itemized list.")
    report = {
        "task_id": trigger.get("task_id"),
        "project_id": trigger.get("project_id"),
        "version_id": trigger.get("version_id"),
        "version": trigger.get("version"),
        "commit_sha": env("CI_COMMIT_SHA"),
        "scan_type": trigger.get("scan_type"),
        "full_scan": trigger.get("full_scan"),
        "previous_version_id": trigger.get("previous_version_id"),
        "status": status.get("status"),
        "severity_counts": counts,
        "counts_source": counts_source,
        "block_severities": block,
        "blocked": blocked,
        "report_only": report_only,
        "items": items or [],
    }
    write_report(env("CODESCAN_REPORT", "codescan-report.json"), report)

    if blocked:
        detail = ", ".join(f"{name}={counts[name]}" for name in hits)
        if report_only:
            log(f"RESULT  : BLOCK_ON_VULN is false -- passing despite blocking "
                f"severities ({detail}); blocking on {block_source}")
            sys.exit(0)
        log(f"RESULT  : BLOCKED by {block_source}: {detail}")
        sys.exit(1)
    log(f"RESULT  : PASSED (blocking on {', '.join(block) if block else 'nothing'};"
        f" {'report-only mode' if report_only else 'findings gate active'})")
    sys.exit(0)


if __name__ == "__main__":
    main()
