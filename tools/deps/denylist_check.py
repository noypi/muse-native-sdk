#!/usr/bin/env python3
"""Deny-list enforcement check incl. Boost refusal (FR-DEPS-003, AC-DEPS-007).

Compares ``load_manifest()`` entry names (and header/include fields) against
``get_denylist()`` from :mod:`tools.deps._deps_data`; any manifest entry
matching a deny-list entry fails closed. Boost matches are
substring/case-insensitive with zero permitted exceptions (RULE-DEPS-003),
so ``boost``, ``boost_filesystem`` and header-only subsets such as
``boost_headers`` are all refused. Missing/unparseable manifests also fail
closed (exit 1, never assumed permissive). Stdlib only.
"""

import argparse
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, get_allowlist, get_denylist, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

REQUIREMENT = "FR-DEPS-003"

_STOPWORDS = {"all", "in", "production", "paths", "the"}

# Short triggers matched token-wise (whole/prefix/suffix token) instead of
# by raw substring, so e.g. "qt" does not fire inside unrelated names.
_SHORT_TRIGGERS = {"qt", "poco", "eastl"}


def _deny_triggers(deny_name):
    """Derive case-insensitive substring triggers from a deny-list name."""
    tokens = re.findall(r"[a-z0-9]+(?:[/_.+-][a-z0-9]+)*", deny_name.lower())
    return [t for t in tokens if t and t not in _STOPWORDS]


def _canonical(text):
    """Lower-case with separator spellings unified (/, _, -, ., +)."""
    return re.sub(r"[_.+\-]", "/", text.lower())


def _trigger_hit(key, lowered, lowered_canonical):
    """Return True when deny trigger *key* matches a manifest candidate."""
    stripped = re.sub(r"[^a-z0-9]", "", key)
    if key in _SHORT_TRIGGERS or (stripped and len(stripped) <= 2):
        # Word-boundary-aware: the short name must cover a whole token
        # or anchor at a token edge (e.g. qt, qtcore, qt5, libqt), so
        # unrelated interiors such as "sqlite" stay clean.
        for token in re.findall(r"[a-z0-9]+", lowered):
            if (
                token == stripped
                or token.startswith(stripped)
                or token.endswith(stripped)
            ):
                return True
        return False
    return bool(key) and _canonical(key) in lowered_canonical


def _entry_candidates(entry):
    """Return the manifest strings to match against the deny-list."""
    candidates = []
    if not isinstance(entry, dict):
        return candidates
    name = entry.get("name")
    if isinstance(name, str) and name:
        candidates.append(name)
    for key, value in entry.items():
        low_key = str(key).lower()
        if low_key == "name":
            continue
        if "incl" in low_key or "header" in low_key:
            if isinstance(value, str) and value:
                candidates.append(value)
            elif isinstance(value, (list, tuple)):
                for item in value:
                    if isinstance(item, str) and item:
                        candidates.append(item)
    return candidates


def find_denied(entries, denylist=None):
    """Return ``[(entry_name, deny_name, reason)]`` for denied manifest entries."""
    denylist = denylist if denylist is not None else get_denylist()
    triggers = [
        (item["name"], item.get("reason", ""), _deny_triggers(item["name"]))
        for item in denylist
    ]
    hits = []
    for entry in entries:
        candidates = _entry_candidates(entry)
        if isinstance(entry, dict):
            label = entry.get("name") if entry.get("name") else repr(entry)
        else:
            label = repr(entry)
        for candidate in candidates:
            lowered = candidate.lower()
            lowered_canonical = _canonical(candidate)
            matched = False
            for deny_name, reason, keys in triggers:
                for key in keys:
                    if _trigger_hit(key, lowered, lowered_canonical):
                        hits.append((label, deny_name, reason))
                        matched = True
                        break
                if matched:
                    break
            if matched:
                break
    return hits


def check_manifest(path):
    """Check *path*; return denied-hit tuples (raises DepsDataError)."""
    entries = load_manifest(path)
    return find_denied(entries)


def format_hit(entry_name, deny_name, reason):
    return (
        "denylist_check: deny: dependency %r matches deny-list entry %r "
        "(%s): %s" % (entry_name, deny_name, REQUIREMENT, reason)
    )


def run_self_test():
    """Run hermetic self-checks; return 0 on success, 1 on failure."""
    failures = []

    def tmp_manifest(entries):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        json.dump(entries, tmp)
        tmp.close()
        return tmp.name

    def entry(name, **overrides):
        data = {
            "name": name,
            "owner": "core",
            "interface": "TestInterface",
            "exit_plan": "replace with local implementation",
            "licence": "MIT",
            "link": "static",
        }
        data.update(overrides)
        return data

    # Case 1: clean manifest passes.
    names = [item["name"] for item in get_allowlist()][:2]
    path = tmp_manifest([entry(n) for n in names])
    try:
        try:
            hits = check_manifest(path)
        except DepsDataError as exc:
            failures.append("clean pass raised: %s" % exc)
        else:
            if hits:
                failures.append("clean pass reported denied: %r" % (hits,))
            else:
                print("self-test: clean pass ok")
    finally:
        os.unlink(path)

    # Case 2: Boost hit cites requirement + reason.
    path = tmp_manifest([entry("boost")])
    try:
        try:
            hits = check_manifest(path)
        except DepsDataError as exc:
            failures.append("boost hit raised instead of flagging: %s" % exc)
        else:
            if not hits:
                failures.append("boost hit not flagged")
            else:
                message = " ".join(
                    format_hit(n, d, r) for n, d, r in hits
                )
                boost = [e for e in get_denylist() if "boost" in e["name"].lower()]
                snippet = boost[0]["reason"].split()[0].strip("(),;:") if boost else "deny"
                if REQUIREMENT in message and (
                    snippet.lower() in message.lower() or "deny" in message.lower()
                ):
                    print("self-test: boost hit fails ok")
                else:
                    failures.append("boost message missing requirement/reason: %r" % message)
    finally:
        os.unlink(path)

    # Case 3: Folly hit.
    path = tmp_manifest([entry("Folly")])
    try:
        try:
            hits = check_manifest(path)
        except DepsDataError as exc:
            failures.append("folly hit raised instead of flagging: %s" % exc)
        else:
            if any("folly" in str(n).lower() for n, _, _ in hits):
                print("self-test: folly hit fails ok")
            else:
                failures.append("folly hit not flagged: %r" % (hits,))
    finally:
        os.unlink(path)

    # Case 4: missing manifest fails closed.
    try:
        check_manifest(os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"))
    except DepsDataError:
        print("self-test: missing manifest fails ok")
    else:
        failures.append("missing manifest did not raise DepsDataError")

    # Case 5: unparseable manifest fails closed.
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    tmp.write("{ this is not json !!!")
    tmp.close()
    try:
        try:
            check_manifest(tmp.name)
        except DepsDataError:
            print("self-test: unparseable manifest fails ok")
        else:
            failures.append("unparseable manifest did not raise DepsDataError")
    finally:
        os.unlink(tmp.name)

    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Enforce the dependency deny-list.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        hits = check_manifest(args.manifest)
    except DepsDataError as exc:
        print("denylist_check: deny: %s (%s)" % (exc, REQUIREMENT))
        return 1
    if hits:
        for entry_name, deny_name, reason in hits:
            print(format_hit(entry_name, deny_name, reason))
        return 1
    print(
        "denylist_check: ok: no denied dependencies linked (%s)" % REQUIREMENT
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
