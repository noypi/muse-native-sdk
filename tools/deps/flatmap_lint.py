#!/usr/bin/env python3
"""Flat-map held-reference lint (FR-DEPS-005, AC-DEPS-011/AC-DEPS-012).

Scans C++ sources under ``--root`` for pointers/references/iterators
into a flat map (``ankerl::unordered_dense``, ``absl::flat_hash_map``)
held across an insert/erase mutation. Any hold whose live range crosses
a mutation on the same map is a violation. Stdlib only.
"""

import argparse
import os
import re
import sys
import tempfile

SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh")

FLAT_MAP_MARKERS = ("unordered_dense", "flat_hash_map")

# Holds: a reference/pointer/iterator bound to map storage.
HOLD_PATTERNS = [
    # auto &ref = map[k]; / auto& ref = map.at(k);
    re.compile(r"auto\s*&\s*\w+\s*=\s*(\w+)\s*(?:\[|\.\s*at\s*\()"),
    # Type &ref = map[k];
    re.compile(r"(?:^|[;{}])\s*\w[\w:<>\s,*&]*&\s*\w+\s*=\s*(\w+)\s*(?:\[|\.\s*at\s*\()"),
    # auto *p = &map[k]; / auto* p = &map.at(k);
    re.compile(r"auto\s*\*\s*\w+\s*=\s*&\s*(\w+)"),
    # Type *p = &map[k];
    re.compile(r"\*\s*\w+\s*=\s*&\s*(\w+)"),
    # const auto& / const auto* variants already covered above; raw address-of element:
    # &map[k] taken without immediate copy is a hold when bound, covered by pointer rules.
    # auto it = map.find(k); / auto it = map.begin();
    re.compile(r"auto\s+\w+\s*=\s*(\w+)\s*\.\s*(?:find|begin|end)\s*\("),
]

# Mutations: rehash-or-move of flat-map storage.
MUTATION_METHOD_RE = re.compile(
    r"(\w+)\s*\.\s*(?:erase|insert|emplace|try_emplace|emplace_hint|erase_if|clear|rehash|reserve)\s*\("
)
MUTATION_SUBSCRIPT_STORE_RE = re.compile(r"(\w+)\s*\[.+?\]\s*=")


def _strip_comments(line):
    idx = line.find("//")
    if idx >= 0:
        return line[:idx]
    return line


def find_holds(lines):
    """Return [(lineno, map_name)] for reference/pointer/iterator holds."""
    holds = []
    for lineno, raw in enumerate(lines, start=1):
        line = _strip_comments(raw)
        for pat in HOLD_PATTERNS:
            m = pat.search(line)
            if m:
                holds.append((lineno, m.group(1)))
                break
    return holds


def find_mutations(lines):
    """Return [(lineno, map_name)] for insert/erase mutations."""
    mutations = []
    for lineno, raw in enumerate(lines, start=1):
        line = _strip_comments(raw)
        for m in MUTATION_METHOD_RE.finditer(line):
            mutations.append((lineno, m.group(1)))
        for m in MUTATION_SUBSCRIPT_STORE_RE.finditer(line):
            # A bare subscript store ``map[k] = v`` is an insertion; the
            # matching hold pattern needs & or *, so this line is not a hold.
            mutations.append((lineno, m.group(1)))
    return mutations


def check_file(path):
    """Check one file; return violation strings (``path:line ...``)."""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError as exc:
        return ["%s:1: AC-DEPS-011 flatmap_lint: cannot read file: %s "
                "(AC-DEPS-012: use integer handle; node-stable container)" % (path, exc)]
    if not any(marker in text for marker in FLAT_MAP_MARKERS):
        return []
    lines = text.splitlines()
    holds = find_holds(lines)
    mutations = find_mutations(lines)
    violations = []
    for hold_line, hold_map in holds:
        for mut_line, mut_map in mutations:
            if mut_line > hold_line and mut_map == hold_map:
                violations.append(
                    "%s:%d: AC-DEPS-011 held reference into flat map '%s' "
                    "across mutation (line %d); use integer handle "
                    "(AC-DEPS-012: use node-stable container)" % (
                        path, hold_line, hold_map, mut_line)
                )
                break
    return violations


def iter_sources(root):
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            if name.endswith(SOURCE_SUFFIXES):
                yield os.path.join(dirpath, name)


def check_root(root):
    """Scan *root*; return violation strings."""
    violations = []
    for path in iter_sources(root):
        violations.extend(check_file(path))
    return violations


def run_self_test():
    """Run hermetic self-checks; return 0 on success, 1 on failure."""
    failures = []

    def scan_text(text):
        tmpdir = tempfile.mkdtemp()
        path = os.path.join(tmpdir, "case.cpp")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        try:
            return check_file(path)
        finally:
            try:
                os.unlink(path)
                os.rmdir(tmpdir)
            except OSError:
                pass

    bad_ref = (
        '#include "ankerl/unordered_dense.h"\n'
        "int lookup(ankerl::unordered_dense<int, std::string> &map, int k) {\n"
        "  auto &ref = map[k];\n"
        "  map.erase(k);\n"
        "  return (int)ref.size();\n"
        "}\n"
    )
    bad_ptr = (
        '#include "ankerl/unordered_dense.h"\n'
        "void grow(ankerl::unordered_dense<int, std::string> &map) {\n"
        "  auto *p = &map[1];\n"
        '  map[2] = "new";\n'
        "  use(*p);\n"
        "}\n"
    )
    bad_iter = (
        '#include "ankerl/unordered_dense.h"\n'
        "void drop(ankerl::unordered_dense<int, std::string> &map) {\n"
        "  auto it = map.find(1);\n"
        "  map.erase(1);\n"
        "  use(it->second);\n"
        "}\n"
    )
    clean = "int add(int a, int b) { return a + b; }\n"
    handle = (
        '#include "ankerl/unordered_dense.h"\n'
        "int lookup(ankerl::unordered_dense<int, std::string> &map, int id) {\n"
        "  auto copy = map[id];\n"
        "  map.erase(id);\n"
        "  return (int)copy.size();\n"
        "}\n"
    )

    for label, text, expect_violation in (
        ("ref across erase", bad_ref, True),
        ("ptr across insert", bad_ptr, True),
        ("iterator across erase", bad_iter, True),
        ("clean source", clean, False),
        ("handle identity copy", handle, False),
    ):
        found = scan_text(text)
        if expect_violation and not found:
            failures.append("%s not flagged" % label)
        elif not expect_violation and found:
            failures.append("%s wrongly flagged: %r" % (label, found))
        else:
            print("self-test: %s ok" % label)

    # Violation text must carry file:line, rule, and node-stable remedy.
    found = scan_text(bad_ref)
    if found:
        blob = found[0]
        if not re.search(r"\S+:\d+", blob):
            failures.append("violation lacks file:line: %r" % (found,))
        if "AC-DEPS-011" not in blob:
            failures.append("violation lacks AC-DEPS-011: %r" % (found,))
        lowered = blob.lower()
        if "ac-deps-012" not in lowered and "node-stable" not in lowered:
            failures.append("violation lacks node-stable remedy: %r" % (found,))
        else:
            print("self-test: violation text ok")
    else:
        failures.append("violation text check has nothing to inspect")

    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Flag flat-map references held across insert/erase.")
    parser.add_argument(
        "--root",
        default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    root = args.root
    if not os.path.isdir(root):
        print("flatmap_lint: deny: scan root %r is missing; failing closed" % (root,))
        return 1
    violations = check_root(root)
    if violations:
        for violation in violations:
            print("flatmap_lint: deny: %s" % violation)
        return 1
    print("flatmap_lint: ok: no held flat-map references across mutation under %s" % root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
