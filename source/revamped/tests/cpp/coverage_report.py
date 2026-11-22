"""Line coverage report for the C++ test suites (reads coverage.ps1's Cobertura output).

    python coverage_report.py coverage.cobertura.xml --source <repo>\\source [--compiled compiled.txt] [--json summary.json] [--top 8]

Per library (uibase/src, bsatk/src, and the modorganizer/src files the tests compile): covered and
instrumented lines from the Cobertura data. A line counted by several modules or functions is
counted once, covered if any of them ran it.

Whole C++ estimate: covered lines over the executable lines of ALL of modorganizer/src plus
uibase/src. Files in the Cobertura data use its line counts; .cpp files the tests never compile
count as fully uncovered, with their executable lines ESTIMATED as non-blank, non-comment lines
(headers outside the data are left out: mostly declarations). Because that count also takes
declarations, braces and #includes, a second figure scales it by the ratio instrumented/NBNC
measured on the .cpp files that were compiled.
"""
import argparse
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

CPP_EXT = ('.cpp', '.cc', '.cxx', '.c')
SRC_EXT = CPP_EXT + ('.h', '.hpp', '.hxx', '.inl', '.inc')


def norm(path):
    return os.path.normcase(os.path.normpath(path))


def load_lines(cobertura):
    """{file: {line: hits}} with the max hits per line."""
    files = {}
    for cls in ET.parse(cobertura).iter('class'):
        name = cls.get('filename')
        if not name:
            continue
        lines = files.setdefault(norm(name), {})
        for line in cls.iter('line'):
            n = int(line.get('number'))
            h = int(line.get('hits', '0'))
            if lines.get(n, -1) < h:
                lines[n] = h
    return files


def nbnc_lines(path):
    """Non-blank lines that aren't only comments (block and line comments, string-naive)."""
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return 0
    count = 0
    in_block = False
    for raw in text.splitlines():
        line = raw
        out = ''
        i = 0
        while i < len(line):
            if in_block:
                end = line.find('*/', i)
                if end < 0:
                    i = len(line)
                else:
                    in_block = False
                    i = end + 2
            else:
                slash = line.find('/', i)
                if slash < 0 or slash + 1 >= len(line):
                    out += line[i:]
                    break
                out += line[i:slash]
                nxt = line[slash + 1]
                if nxt == '/':
                    break
                if nxt == '*':
                    in_block = True
                    i = slash + 2
                else:
                    out += '/'
                    i = slash + 1
        if out.strip():
            count += 1
    return count


def walk(root, exts):
    for d, _, names in os.walk(root):
        for n in names:
            if n.lower().endswith(exts):
                yield norm(os.path.join(d, n))


def under(path, root):
    return path.startswith(root + os.sep)


def stats(lines):
    return sum(1 for h in lines.values() if h > 0), len(lines)


def pct(c, t):
    return 100.0 * c / t if t else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cobertura')
    ap.add_argument('--source', required=True, help='the repo\'s source folder')
    ap.add_argument('--compiled', help='list of the sources the coverage builds compiled, one per line')
    ap.add_argument('--json')
    ap.add_argument('--top', type=int, default=8)
    a = ap.parse_args()

    src = norm(a.source)
    roots = {
        'uibase': norm(os.path.join(src, 'upstream', 'modorganizer_super', 'uibase', 'src')),
        'bsatk': norm(os.path.join(src, 'upstream', 'modorganizer_super', 'bsatk', 'src')),
        'organizer': norm(os.path.join(src, 'modorganizer', 'src')),
    }
    data = load_lines(a.cobertura)
    compiled = set()
    if a.compiled and os.path.exists(a.compiled):
        with open(a.compiled, encoding='utf-8-sig') as f:
            compiled = {norm(l.strip()) for l in f if l.strip()}
    summary = {'libraries': {}}

    for lib, root in roots.items():
        files = {p: stats(l) for p, l in data.items() if under(p, root) and l}
        cov = sum(c for c, _ in files.values())
        tot = sum(t for _, t in files.values())
        top = sorted(files.items(), key=lambda kv: (-kv[1][1], kv[0]))[:a.top]
        summary['libraries'][lib] = {
            'covered': cov, 'total': tot, 'percent': round(pct(cov, tot), 1), 'files': len(files),
            'top': [{'file': os.path.relpath(p, root), 'covered': c, 'total': t, 'percent': round(pct(c, t), 1)}
                    for p, (c, t) in top],
        }
        print(f'{lib:10} {cov:6}/{tot:<6} {pct(cov, tot):5.1f}%  ({len(files)} files)')
        for p, (c, t) in top:
            print(f'    {os.path.relpath(p, root):40} {c:5}/{t:<5} {pct(c, t):5.1f}%')

    # whole C++ estimate over modorganizer/src + uibase/src (+ bsatk/src)
    def whole(libs):
        cov = tot_meas = est = 0
        nbnc_compiled = valid_compiled = 0
        never = 0
        for lib in libs:
            root = roots[lib]
            for p, l in data.items():
                if under(p, root) and l:
                    c, t = stats(l)
                    cov += c
                    tot_meas += t
                    if p.endswith(CPP_EXT):
                        nbnc_compiled += nbnc_lines(p)
                        valid_compiled += t
            for p in walk(root, CPP_EXT):
                if (p not in data or not data[p]) and p not in compiled:
                    est += nbnc_lines(p)
                    never += 1
        ratio = valid_compiled / nbnc_compiled if nbnc_compiled else 1.0
        raw_tot = tot_meas + est
        scaled_tot = tot_meas + est * ratio
        return {
            'covered': cov, 'measured_total': tot_meas, 'uncompiled_cpp_files': never,
            'uncompiled_nbnc_estimate': est, 'total_estimate': raw_tot,
            'percent_estimate': round(pct(cov, raw_tot), 1),
            'instrumented_per_nbnc': round(ratio, 3),
            'total_scaled': round(scaled_tot), 'percent_scaled': round(pct(cov, scaled_tot), 1),
        }

    print('\nwhole C++ (ESTIMATE: uncompiled .cpp files counted as uncovered, sized by non-blank non-comment lines)')
    for key, libs in (('organizer+uibase', ['organizer', 'uibase']), ('organizer+uibase+bsatk', ['organizer', 'uibase', 'bsatk'])):
        w = whole(libs)
        summary[key] = w
        print(f'  {key:24} {w["covered"]}/{w["total_estimate"]} = {w["percent_estimate"]}%'
              f'  (measured {w["measured_total"]}, + ~{w["uncompiled_nbnc_estimate"]} in {w["uncompiled_cpp_files"]} uncompiled .cpp)'
              f'; scaled by instrumented/NBNC {w["instrumented_per_nbnc"]}: {w["covered"]}/{w["total_scaled"]} = {w["percent_scaled"]}%')

    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(summary, f, indent=2)
    return 0


if __name__ == '__main__':
    sys.exit(main())
