#!/usr/bin/env python3
"""Dry-run scope gate (FR-TESTING-005 / AC-TESTING-036 / AC-TESTING-037).

Hermetic test doubles stay in scope: a double-like file or symbol found
outside the allowlisted unit-test dirs, golden tooling, and named
dry-run paths is a violation. Every named dry-run path must emit no
shipped artifact. Missing inputs fail closed. Stdlib only; walks the
local tree only and performs no network I/O.

Usage:
    dry_run_scope_gate.py --scope <path> [--allowlist <path>]
"""

import argparse
import os
import sys

_DEFAULT_ALLOWLIST = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "scope-allowlist.txt"
)

# Case-insensitive name fragments marking a hermetic test double; mirrors
# the tools/deps/production_link_check.py double pattern.
_DOUBLE_SUBSTRINGS = ("mock", "fake", "stub", "double")

# Shipped-artifact suffixes that must never appear under a named dry-run
# path (and never outside the allowlist).
_SHIPPED_SUFFIXES = (".aar", ".so", ".jar")


def _load_allowlist(allowlist_path):
    """Read allowlist prefixes; ignore blanks and '#' comments."""
    with open(allowlist_path, "r", encoding="utf-8") as handle:
        raw = handle.read().splitlines()
    entries = []
    for line in raw:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        entries.append(stripped.strip("/"))
    return entries


def _covered(relpath, entries):
    """Return True when tree-relative *relpath* falls under an entry."""
    for entry in entries:
        if not entry:
            continue
        if relpath == entry or relpath.startswith(entry + "/"):
            return True
    return False


def _under_dry_run_entry(relpath, entries):
    """Return True when *relpath* falls under a dry-run allowlist entry."""
    for entry in entries:
        if not entry or "dry-run" not in entry:
            continue
        if relpath == entry or relpath.startswith(entry + "/"):
            return True
    return False


def _is_double_like(name):
    """Return True when *name* looks like a hermetic test double."""
    lowered = name.lower()
    return any(fragment in lowered for fragment in _DOUBLE_SUBSTRINGS)


def _file_content_has_double(path):
    """Return True when a .py file's text mentions a double fragment."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            content = handle.read()
    except OSError:
        return False
    lowered = content.lower()
    return any(fragment in lowered for fragment in _DOUBLE_SUBSTRINGS)


def check_scope(tree, allowlist_path):
    """Check *tree* against *allowlist_path*; return violation strings.

    Args:
        tree: Root directory of the source tree to scan.
        allowlist_path: File with one tree-relative prefix per line.

    Returns:
        A list of violation strings; empty when the tree is clean.

    Raises:
        FileNotFoundError: When *tree* or *allowlist_path* is missing.
    """
    if not os.path.isdir(tree):
        raise FileNotFoundError("scope tree not found: %s" % (tree,))
    if not os.path.isfile(allowlist_path):
        raise FileNotFoundError("allowlist not found: %s" % (allowlist_path,))
    entries = _load_allowlist(allowlist_path)
    violations = []
    for dirpath, _dirnames, filenames in os.walk(tree):
        for filename in filenames:
            full = os.path.join(dirpath, filename)
            rel = os.path.relpath(full, tree).replace(os.sep, "/")
            lowered_rel = rel.lower()
            is_shipped = lowered_rel.endswith(_SHIPPED_SUFFIXES)
            if is_shipped and (
                _under_dry_run_entry(rel, entries) or not _covered(rel, entries)
            ):
                violations.append(
                    "dry_run_scope_gate: deny: shipped artifact %r under "
                    "dry-run scope; dry-run paths must emit no shipped "
                    "artifact" % (rel,)
                )
                continue
            if _covered(rel, entries):
                continue
            if _is_double_like(filename):
                violations.append(
                    "dry_run_scope_gate: deny: %r looks like a hermetic "
                    "test double outside the allowlisted scope" % (rel,)
                )
            elif filename.endswith(".py") and _file_content_has_double(full):
                violations.append(
                    "dry_run_scope_gate: deny: %r defines a hermetic "
                    "test double symbol outside the allowlisted scope"
                    % (rel,)
                )
    return sorted(violations)


def main(argv=None):
    """CLI entry point; 0 pass, 1 fail-closed/violation, 2 usage error."""
    parser = argparse.ArgumentParser(
        description="Gate hermetic test doubles to the scope allowlist."
    )
    parser.add_argument("--scope", required=True, help="Tree root to scan.")
    parser.add_argument(
        "--allowlist",
        default=_DEFAULT_ALLOWLIST,
        help="Allowlist file (default: scope-allowlist.txt).",
    )
    args = parser.parse_args(argv)
    try:
        violations = check_scope(args.scope, args.allowlist)
    except (FileNotFoundError, NotADirectoryError, OSError) as exc:
        print("dry_run_scope_gate: deny: %s" % (exc,))
        return 1
    if violations:
        for violation in violations:
            print(violation)
        return 1
    print("dry_run_scope_gate: ok: no out-of-scope hermetic doubles")
    return 0


__all__ = ["check_scope", "main"]


if __name__ == "__main__":
    sys.exit(main())
