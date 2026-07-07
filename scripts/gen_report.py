#!/usr/bin/env python3
"""Generate an HTML coverage + test-result dashboard for lib_srs.

Uses only the Python standard library plus the system `gcov` binary.
Produces:
  <out>/index.html                       - dashboard (matches the reference style)
  <out>/coverage/<file>.html             - annotated per-source drill-down
  <out>/tests/all-components-tests.html  - flat test list from gtest XML

Invoked by the `coverage` CMake target; see gtest/CMakeLists.txt.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import html
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path


# ---------------------------------------------------------------------------
# gcov collection
# ---------------------------------------------------------------------------

@dataclass
class FileCoverage:
    path: Path                     # absolute source path
    lines: dict[int, int] = field(default_factory=dict)  # line_no -> hits

    def merge(self, other: "FileCoverage") -> None:
        for ln, hits in other.lines.items():
            self.lines[ln] = self.lines.get(ln, 0) + hits

    @property
    def total(self) -> int:
        return len(self.lines)

    @property
    def covered(self) -> int:
        return sum(1 for h in self.lines.values() if h > 0)

    @property
    def percent(self) -> float:
        return (self.covered * 100.0 / self.total) if self.total else 0.0


_GCOV_LINE = re.compile(r"^\s*(?P<hits>[^:]+):\s*(?P<lineno>\d+):(?P<rest>.*)$")


def _parse_gcov_file(gcov_path: Path) -> FileCoverage | None:
    """Parse a single .gcov file. Returns None if it has no Source: header."""
    try:
        text = gcov_path.read_text(errors="replace")
    except OSError:
        return None
    src_path: Path | None = None
    lines: dict[int, int] = {}
    for raw in text.splitlines():
        m = _GCOV_LINE.match(raw)
        if not m:
            continue
        hits_s = m.group("hits").strip()
        lineno = int(m.group("lineno"))
        rest = m.group("rest")
        if lineno == 0:
            if rest.startswith("Source:"):
                src_path = Path(rest[len("Source:"):]).resolve()
            continue
        if hits_s == "-":
            continue                        # non-executable
        if hits_s.startswith(("#", "=")):
            lines[lineno] = 0
        else:
            # strip * suffix used for exceptional paths in some gcc versions
            hits_s = hits_s.rstrip("*")
            try:
                lines[lineno] = int(hits_s)
            except ValueError:
                continue
    if src_path is None:
        return None
    return FileCoverage(path=src_path, lines=lines)


def collect_coverage(build_dir: Path, source_dir: Path) -> dict[Path, FileCoverage]:
    """Run gcov against every .gcno under build_dir and aggregate by source file."""
    gcov_bin = shutil.which("gcov")
    if not gcov_bin:
        sys.exit("error: `gcov` not found on PATH")

    # gcov emits .gcov files into cwd; use a scratch dir under build.
    scratch = build_dir / "coverage" / ".gcov-scratch"
    scratch.mkdir(parents=True, exist_ok=True)
    for stale in scratch.glob("*.gcov"):
        stale.unlink()

    gcno_files = list(build_dir.rglob("*.gcno"))
    if not gcno_files:
        sys.exit("error: no .gcno files found -- configure with -DENABLE_COVERAGE=ON and rebuild")

    # -p: preserve full path in output name, -b: branch info in the file
    for gcno in gcno_files:
        subprocess.run(
            [gcov_bin, "-p", "-b", str(gcno)],
            cwd=scratch,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )

    coverage: dict[Path, FileCoverage] = {}
    src_root = source_dir.resolve()
    for gcov_out in scratch.glob("*.gcov"):
        fc = _parse_gcov_file(gcov_out)
        if fc is None:
            continue
        # Keep only files inside the requested source tree.
        try:
            fc.path.relative_to(src_root)
        except ValueError:
            continue
        existing = coverage.get(fc.path)
        if existing is None:
            coverage[fc.path] = fc
        else:
            existing.merge(fc)
    return coverage


# ---------------------------------------------------------------------------
# gtest XML parsing
# ---------------------------------------------------------------------------

@dataclass
class TestSuite:
    name: str
    passed: int
    failed: int
    disabled: int
    total: int
    time: float
    cases: list[tuple[str, str, str]] = field(default_factory=list)  # (name, status, msg)

    @property
    def pass_rate(self) -> float:
        return (self.passed * 100.0 / self.total) if self.total else 0.0


def parse_gtest_xml(xml_path: Path) -> list[TestSuite]:
    if not xml_path.exists():
        return []
    root = ET.parse(xml_path).getroot()
    suites: list[TestSuite] = []
    for suite_el in root.findall("testsuite"):
        name = suite_el.get("name", "?")
        total = int(suite_el.get("tests", "0"))
        failures = int(suite_el.get("failures", "0"))
        disabled = int(suite_el.get("disabled", "0"))
        errors = int(suite_el.get("errors", "0"))
        passed = total - failures - errors - disabled
        cases: list[tuple[str, str, str]] = []
        for case_el in suite_el.findall("testcase"):
            cn = case_el.get("name", "?")
            failure = case_el.find("failure")
            if failure is not None:
                cases.append((cn, "FAILED", (failure.get("message") or "").strip()))
            elif case_el.get("status") == "notrun":
                cases.append((cn, "DISABLED", ""))
            else:
                cases.append((cn, "PASSED", ""))
        suites.append(TestSuite(
            name=name,
            passed=passed,
            failed=failures + errors,
            disabled=disabled,
            total=total,
            time=float(suite_el.get("time", "0") or 0.0),
            cases=cases,
        ))
    return suites


# ---------------------------------------------------------------------------
# HTML rendering
# ---------------------------------------------------------------------------

_CSS = """
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
       background: #f5f7fa; padding: 20px; min-height: 100vh; color: #1a202c; }
.container { max-width: 1600px; margin: 0 auto; }
.header { background: white; border-radius: 8px; padding: 24px 32px; margin-bottom: 24px;
          box-shadow: 0 1px 3px rgba(0,0,0,.1); border-left: 4px solid #3182ce; }
.header h1 { font-size: 24px; font-weight: 600; margin-bottom: 4px; }
.header p { color: #718096; font-size: 14px; }
.back-link { display: inline-flex; align-items: center; background: white; color: #3182ce;
             padding: 10px 18px; border-radius: 6px; text-decoration: none; font-weight: 600;
             font-size: 14px; margin-bottom: 20px; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
.dashboard-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(500px, 1fr));
                  gap: 24px; margin-bottom: 30px; }
.dashboard-card { background: white; border-radius: 8px; padding: 30px;
                  box-shadow: 0 1px 3px rgba(0,0,0,.1); }
.dashboard-title { font-size: 18px; font-weight: 600; color: #2d3748; margin-bottom: 20px; }
.big-number { font-size: 48px; font-weight: 700; text-align: center; margin-bottom: 10px; }
.big-label { text-align: center; color: #718096; font-size: 14px; margin-bottom: 20px; }
.mini-stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); gap: 15px; }
.mini-stat { text-align: center; padding: 12px; border-radius: 6px; }
.mini-value { font-size: 20px; font-weight: 700; }
.mini-label { font-size: 11px; color: #718096; margin-top: 4px; text-transform: uppercase; letter-spacing: 0.5px; }
.components-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 20px; }
.component-card { background: white; border-radius: 8px; padding: 25px;
                  box-shadow: 0 1px 3px rgba(0,0,0,.1); transition: transform .2s, box-shadow .2s; }
.component-card:hover { transform: translateY(-5px); box-shadow: 0 8px 12px rgba(0,0,0,.15); }
.component-name { font-size: 20px; font-weight: 600; color: #2d3748; margin-bottom: 15px; word-break: break-all; }
.metric-row { display: flex; justify-content: space-between; align-items: center;
              padding: 12px 0; border-bottom: 1px solid #f0f0f0; }
.metric-row:last-child { border-bottom: none; }
.metric-label { color: #718096; font-size: 13px; }
.metric-value { font-weight: 600; font-size: 15px; }
.view-links { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 20px; }
.view-link { display: block; text-align: center; padding: 10px; border-radius: 6px;
             text-decoration: none; font-weight: 600; font-size: 13px; }
.link-tests { background: #3182ce; color: white; }
.link-coverage { background: #38a169; color: white; }
.footer { text-align: center; color: #718096; margin-top: 30px; font-size: 13px; }
table.tests { width: 100%; border-collapse: collapse; background: white; border-radius: 8px;
              overflow: hidden; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
table.tests th, table.tests td { padding: 10px 14px; text-align: left; font-size: 13px;
                                 border-bottom: 1px solid #f0f0f0; }
table.tests th { background: #edf2f7; font-weight: 600; }
.status-PASSED { color: #22543d; font-weight: 600; }
.status-FAILED { color: #742a2a; font-weight: 600; }
.status-DISABLED { color: #744210; font-weight: 600; }
table.source { width: 100%; border-collapse: collapse; font-family: 'SFMono-Regular', Consolas, monospace;
               font-size: 12px; background: white; border-radius: 8px; overflow: hidden;
               box-shadow: 0 1px 3px rgba(0,0,0,.1); }
table.source td { padding: 2px 10px; white-space: pre; vertical-align: top; }
table.source td.lineno { text-align: right; color: #a0aec0; width: 60px; user-select: none; }
table.source td.hits { text-align: right; width: 70px; user-select: none; }
table.source tr.cov { background: #f0fff4; }
table.source tr.cov td.hits { color: #22543d; }
table.source tr.nocov { background: #fff5f5; }
table.source tr.nocov td.hits { color: #742a2a; font-weight: 700; }
table.source tr.nonx td.hits { color: #cbd5e0; }
"""


def _color_for(pct: float) -> str:
    if pct >= 80:
        return "#38a169"       # green
    if pct >= 60:
        return "#dd6b20"       # orange
    return "#e53e3e"           # red


def _fmt_pct(pct: float) -> str:
    return f"{pct:.1f}%"


def render_source_page(fc: FileCoverage, out: Path, rel_display: str) -> None:
    try:
        src_text = fc.path.read_text(errors="replace").splitlines()
    except OSError:
        src_text = []
    rows: list[str] = []
    for i, line in enumerate(src_text, start=1):
        hits = fc.lines.get(i)
        if hits is None:
            cls = "nonx"
            hits_s = ""
        elif hits > 0:
            cls = "cov"
            hits_s = str(hits)
        else:
            cls = "nocov"
            hits_s = "0"
        rows.append(
            f'<tr class="{cls}"><td class="lineno">{i}</td>'
            f'<td class="hits">{hits_s}</td>'
            f'<td>{html.escape(line)}</td></tr>'
        )
    body = (
        f'<a href="../index.html" class="back-link">&larr; Back to Dashboard</a>'
        f'<div class="header"><h1>{html.escape(rel_display)}</h1>'
        f'<p>Coverage: <strong style="color:{_color_for(fc.percent)}">{_fmt_pct(fc.percent)}</strong> '
        f'&mdash; {fc.covered} / {fc.total} lines covered</p></div>'
        f'<table class="source">{"".join(rows)}</table>'
    )
    _write_page(out, f"Coverage: {rel_display}", body)


def render_tests_page(suites: list[TestSuite], out: Path) -> None:
    rows: list[str] = []
    for suite in suites:
        for case_name, status, msg in suite.cases:
            rows.append(
                f"<tr><td>{html.escape(suite.name)}</td>"
                f"<td>{html.escape(case_name)}</td>"
                f'<td class="status-{status}">{status}</td>'
                f"<td>{html.escape(msg)}</td></tr>"
            )
    body = (
        '<a href="../index.html" class="back-link">&larr; Back to Dashboard</a>'
        '<div class="header"><h1>All Test Cases</h1>'
        f'<p>{sum(s.total for s in suites)} total &mdash; '
        f'{sum(s.passed for s in suites)} passed, '
        f'{sum(s.failed for s in suites)} failed, '
        f'{sum(s.disabled for s in suites)} disabled</p></div>'
        '<table class="tests"><tr><th>Suite</th><th>Test</th><th>Status</th><th>Message</th></tr>'
        + "".join(rows) + "</table>"
    )
    _write_page(out, "All Test Cases", body)


def render_dashboard(
    project: str,
    build_id: str,
    coverage: dict[Path, FileCoverage],
    suites: list[TestSuite],
    src_root: Path,
    out: Path,
) -> None:
    total_lines = sum(fc.total for fc in coverage.values())
    covered_lines = sum(fc.covered for fc in coverage.values())
    overall_pct = (covered_lines * 100.0 / total_lines) if total_lines else 0.0

    total_tests = sum(s.total for s in suites)
    total_passed = sum(s.passed for s in suites)
    total_failed = sum(s.failed for s in suites)
    total_disabled = sum(s.disabled for s in suites)
    test_pct = (total_passed * 100.0 / total_tests) if total_tests else 0.0

    # per-file coverage mini-stats
    mini_stats = "".join(
        f'<div class="mini-stat" style="background:#e6fffa;">'
        f'<div class="mini-value" style="color:#234e52;">{fc.covered}</div>'
        f'<div class="mini-label">{html.escape(fc.path.stem)}</div></div>'
        for fc in sorted(coverage.values(), key=lambda f: f.path.name)
    )

    # component cards -- one per source file
    cards: list[str] = []
    for fc in sorted(coverage.values(), key=lambda f: f.path.name):
        rel = fc.path.relative_to(src_root)
        page = out / "coverage" / f"{rel.as_posix().replace('/', '_')}.html"
        render_source_page(fc, page, str(rel))
        cards.append(
            '<div class="component-card">'
            f'<div class="component-name">{html.escape(str(rel))}</div>'
            '<div class="metric-row"><span class="metric-label">Code Coverage</span>'
            f'<span class="metric-value" style="color:{_color_for(fc.percent)}">{_fmt_pct(fc.percent)}</span></div>'
            '<div class="metric-row"><span class="metric-label">Lines Covered</span>'
            f'<span class="metric-value">{fc.covered:,} / {fc.total:,}</span></div>'
            '<div class="view-links">'
            f'<a href="coverage/{page.name}" class="view-link link-coverage">View Coverage &rarr;</a>'
            '<a href="tests/all-components-tests.html" class="view-link link-tests">View Tests &rarr;</a>'
            '</div></div>'
        )

    body = (
        '<div class="header">'
        f'<h1>{html.escape(project)} Component Dashboard | Build: {html.escape(build_id)}</h1>'
        '<p>Unified coverage and test results analysis</p>'
        f'<div style="text-align:right;color:#718096;font-size:13px;margin-top:8px;font-weight:500;">'
        f'&#128230; Build: {html.escape(build_id)}</div></div>'
        '<div class="dashboard-grid">'
        '<div class="dashboard-card"><div class="dashboard-title">Code Coverage</div>'
        f'<div class="big-number" style="color:{_color_for(overall_pct)}">{_fmt_pct(overall_pct)}</div>'
        f'<div class="big-label">{covered_lines:,} / {total_lines:,} lines covered</div>'
        f'<div class="mini-stats">{mini_stats}</div></div>'
        '<div class="dashboard-card"><div class="dashboard-title">Test Results</div>'
        f'<div class="big-number" style="color:{_color_for(test_pct)}">{test_pct:.2f}%</div>'
        f'<div class="big-label">{total_passed:,} / {total_tests:,} tests passed</div>'
        '<div class="mini-stats">'
        f'<div class="mini-stat" style="background:#c6f6d5;"><div class="mini-value" style="color:#22543d;">{total_passed}</div><div class="mini-label">Passed</div></div>'
        f'<div class="mini-stat" style="background:#fed7d7;"><div class="mini-value" style="color:#742a2a;">{total_failed}</div><div class="mini-label">Failed</div></div>'
        f'<div class="mini-stat" style="background:#faf089;"><div class="mini-value" style="color:#744210;">{total_disabled}</div><div class="mini-label">Disabled</div></div>'
        f'<div class="mini-stat" style="background:#e2e8f0;"><div class="mini-value" style="color:#2d3748;">{total_tests}</div><div class="mini-label">Total</div></div>'
        '</div><div style="text-align:center;margin-top:16px;">'
        '<a href="tests/all-components-tests.html" style="display:inline-block;padding:10px 20px;background:#3182ce;color:white;text-decoration:none;border-radius:6px;font-weight:600;font-size:13px;">View Full Test Results</a>'
        '</div></div></div>'
        f'<div class="components-grid">{"".join(cards)}</div>'
    )
    _write_page(out / "index.html", f"{project} Dashboard | Build: {build_id}", body,
                footer=f"Generated on {_dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")


def _write_page(path: Path, title: str, body: str, footer: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    doc = (
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        f"<title>{html.escape(title)}</title><style>{_CSS}</style></head>"
        f"<body><div class='container'>{body}"
        + (f"<div class='footer'><p>{html.escape(footer)}</p></div>" if footer else "")
        + "</div></body></html>"
    )
    path.write_text(doc, encoding="utf-8")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", type=Path, required=True)
    ap.add_argument("--source-dir", type=Path, required=True,
                    help="Only files under this directory are reported.")
    ap.add_argument("--gtest-xml", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--project-name", default="service_recovery_scheduler")
    ap.add_argument("--build-id", default="local")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    coverage = collect_coverage(args.build_dir.resolve(), args.source_dir.resolve())
    suites = parse_gtest_xml(args.gtest_xml)
    render_tests_page(suites, args.out / "tests" / "all-components-tests.html")
    render_dashboard(args.project_name, args.build_id, coverage, suites,
                     args.source_dir.resolve(), args.out)
    print(f"[gen_report] wrote {args.out / 'index.html'}")
    print(f"[gen_report] files: {len(coverage)}   suites: {len(suites)}   "
          f"tests: {sum(s.total for s in suites)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
