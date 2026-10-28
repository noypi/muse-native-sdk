#!/usr/bin/env python3
"""FR-NAV-010 T11: Launch-mode / task-affinity contradiction lint.

Interface:
  `python3 toolchain/launch-mode-lint/lint.py --manifest <manifest.json>`
  exits 0 when clean, 2 listing contradictions on stdout.

Manifest shape (extra fields ignored, not rejected):
  {"graphs": [{"graphId": str, "launchMode": str, "taskAffinity": str,
               "appLinkTarget": bool}]}
with launchMode in singleTop/singleTask/singleInstance.

Contradiction rules:
  R1: two `singleInstance` graphs sharing one `taskAffinity` contradict.
  R2: an App-Link target (`appLinkTarget: true`) inside a `singleInstance`
      graph contradicts (Q-NAV-001 caution).

Clean sharing: `singleTop`/`singleTask` graphs may share an affinity.
stdlib only.
"""

import argparse
import json
import os
import sys


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def find_contradictions(manifest):
    contradictions = []
    graphs = manifest.get("graphs", []) if isinstance(manifest, dict) else []
    if not isinstance(graphs, list):
        return ["manifest: 'graphs' must be a list"]

    # R2: App-Link target inside singleInstance.
    for g in graphs:
        if not isinstance(g, dict):
            continue
        if g.get("launchMode") == "singleInstance" and g.get("appLinkTarget") is True:
            contradictions.append(
                "R2 applink-in-singleInstance: graph '%s' is singleInstance "
                "with appLinkTarget=true" % g.get("graphId", "?")
            )

    # R1: two singleInstance graphs sharing one taskAffinity.
    by_affinity = {}
    for g in graphs:
        if not isinstance(g, dict):
            continue
        if g.get("launchMode") != "singleInstance":
            continue
        affinity = g.get("taskAffinity", "")
        by_affinity.setdefault(affinity, []).append(
            g.get("graphId", "?"))
    for affinity, ids in sorted(by_affinity.items()):
        if len(ids) > 1:
            contradictions.append(
                "R1 shared-singleInstance-affinity '%s': graphs %s"
                % (affinity, ", ".join("'%s'" % i for i in ids))
            )
    return contradictions


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Lint launchMode/taskAffinity contradictions.")
    parser.add_argument("--manifest", required=True,
                        help="Path to route manifest JSON.")
    args = parser.parse_args(argv)

    if not os.path.isfile(args.manifest):
        print("error: manifest not found: %s" % args.manifest,
              file=sys.stderr)
        return 2
    try:
        manifest = load_manifest(args.manifest)
    except (OSError, ValueError) as exc:
        print("error: cannot read manifest: %s" % exc, file=sys.stderr)
        return 2

    contradictions = find_contradictions(manifest)
    if contradictions:
        for line in contradictions:
            print(line)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
