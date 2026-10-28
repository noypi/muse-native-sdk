#!/usr/bin/env python3
"""FR-NAV-006 T2: App Links codegen — one manifest in, two artifacts out.

Reads the single-source route manifest (see
engine/navigation/applinks_manifest.schema.json) and emits:

  <out>/intent-filters.xml  — Android <intent-filter android:autoVerify="true">
                              with <action VIEW>, <category BROWSABLE/DEFAULT>,
                              and one <data android:scheme/host/pathPattern>
                              per route x scheme.
  <out>/assetlinks.json      — .well-known/assetlinks.json payload, exactly
                              [{relation: ["delegate_permission/common.
                              handle_all_urls"], target: {namespace,
                              package_name, sha256_cert_fingerprints}}].

Interface:
  generate(manifestPath: string, outDir: string):
      {intentFiltersXml: string, assetlinksJson: string}
      | CodegenError{reason: DRIFT | SCHEMA_MISMATCH, detail: string}
  CLI `codegen.py --manifest <f> --out <dir> --check` exits 0 on match,
  2 on drift with unified diff on stdout.

Structural validation only (missing/empty fields, wrong types). Regex
compilability of pathPattern (BAD_PATTERN) is a loader concern, not
codegen's — it is not checked here.
"""

import argparse
import difflib
import json
import os
import sys
from xml.sax.saxutils import escape

INTENT_FILTERS_FILENAME = "intent-filters.xml"
ASSETLINKS_FILENAME = "assetlinks.json"

RELATION_HANDLE_ALL_URLS = "delegate_permission/common.handle_all_urls"


class CodegenError(Exception):
    """CodegenError{reason: DRIFT | SCHEMA_MISMATCH, detail: string}."""

    def __init__(self, reason, detail):
        super().__init__("%s: %s" % (reason, detail))
        self.reason = reason
        self.detail = detail


def _fail(reason, detail):
    return CodegenError(reason, detail)


def load_manifest(manifest_path):
    try:
        with open(manifest_path, "r", encoding="utf-8") as fh:
            manifest = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        raise _fail("SCHEMA_MISMATCH",
                    "cannot parse manifest '%s': %s"
                    % (manifest_path, exc))
    if not isinstance(manifest, dict):
        raise _fail("SCHEMA_MISMATCH", "manifest root must be an object")
    return manifest


def validate_structure(manifest):
    """Structural check only. Raises CodegenError(SCHEMA_MISMATCH) on failure."""
    for field in ("hosts", "schemes", "routes",
                  "packageName", "sha256CertFingerprints"):
        if field not in manifest:
            raise _fail("SCHEMA_MISMATCH",
                        "missing required field '%s'" % field)
    hosts = manifest["hosts"]
    if (not isinstance(hosts, list) or not hosts
            or not all(isinstance(h, str) and h for h in hosts)):
        raise _fail("SCHEMA_MISMATCH",
                    "field 'hosts' must be a non-empty string array")
    schemes = manifest["schemes"]
    if (not isinstance(schemes, list) or not schemes
            or not all(isinstance(s, str) and s for s in schemes)):
        raise _fail("SCHEMA_MISMATCH",
                    "field 'schemes' must be a non-empty string array")
    routes = manifest["routes"]
    if not isinstance(routes, list) or not routes:
        raise _fail("SCHEMA_MISMATCH",
                    "field 'routes' must be a non-empty array")
    for i, route in enumerate(routes):
        prefix = "routes[%d]" % i
        if not isinstance(route, dict):
            raise _fail("SCHEMA_MISMATCH",
                        "%s must be an object" % prefix)
        for field in ("host", "pathPattern", "routeId", "query"):
            if field not in route:
                raise _fail("SCHEMA_MISMATCH",
                            "missing required field '%s.%s'" % (prefix, field))
        if not isinstance(route["host"], str) or not route["host"]:
            raise _fail("SCHEMA_MISMATCH",
                        "field '%s.host' must be a non-empty string" % prefix)
        if (not isinstance(route["pathPattern"], str)
                or not route["pathPattern"].startswith("/")):
            raise _fail("SCHEMA_MISMATCH",
                        "field '%s.pathPattern' must be a string "
                        "starting with '/'" % prefix)
        if not isinstance(route["routeId"], str) or not route["routeId"]:
            raise _fail("SCHEMA_MISMATCH",
                        "field '%s.routeId' must be a non-empty string"
                        % prefix)
        if not isinstance(route["query"], list):
            raise _fail("SCHEMA_MISMATCH",
                        "field '%s.query' must be an array" % prefix)
    package_name = manifest["packageName"]
    if not isinstance(package_name, str) or not package_name:
        raise _fail("SCHEMA_MISMATCH",
                    "field 'packageName' must be a non-empty string")
    fps = manifest["sha256CertFingerprints"]
    if not isinstance(fps, list) or not fps:
        raise _fail("SCHEMA_MISMATCH",
                    "field 'sha256CertFingerprints' must be a non-empty "
                    "array")
    for fp in fps:
        if not isinstance(fp, str) or not fp:
            raise _fail("SCHEMA_MISMATCH",
                        "field 'sha256CertFingerprints' entries must be "
                        "non-empty strings")
    auto_verify = manifest.get("autoVerify", True)
    if not isinstance(auto_verify, bool):
        raise _fail("SCHEMA_MISMATCH",
                    "field 'autoVerify' must be a boolean")
    return auto_verify


def render_intent_filters(manifest, auto_verify):
    lines = []
    lines.append('<?xml version="1.0" encoding="utf-8"?>')
    lines.append('<intent-filters>')
    lines.append('  <intent-filter android:autoVerify="%s">'
                 % ("true" if auto_verify else "false"))
    lines.append('    <action android:name="android.intent.action.VIEW" />')
    lines.append('    <category '
                 'android:name="android.intent.category.BROWSABLE" />')
    lines.append('    <category android:name="android.intent.category.DEFAULT"'
                 ' />')
    for route in manifest["routes"]:
        for scheme in manifest["schemes"]:
            lines.append('    <data android:scheme="%s" android:host="%s" '
                         'android:pathPattern="%s" />'
                         % (escape(scheme, {'"': "&quot;"}),
                            escape(route["host"], {'"': "&quot;"}),
                            escape(route["pathPattern"], {'"': "&quot;"})))
    lines.append('  </intent-filter>')
    lines.append('</intent-filters>')
    return "\n".join(lines) + "\n"


def render_assetlinks(manifest):
    entries = [{
        "relation": [RELATION_HANDLE_ALL_URLS],
        "target": {
            "namespace": "android_app",
            "package_name": manifest["packageName"],
            "sha256_cert_fingerprints":
                list(manifest["sha256CertFingerprints"]),
        },
    }]
    return json.dumps(entries, indent=2) + "\n"


def generate(manifest_path, out_dir):
    """generate(manifestPath, outDir) -> {intentFiltersXml, assetlinksJson}.

    Raises CodegenError(SCHEMA_MISMATCH) on structural problems.
    Writes intent-filters.xml and assetlinks.json into out_dir and
    returns their contents.
    """
    manifest = load_manifest(manifest_path)
    auto_verify = validate_structure(manifest)
    xml = render_intent_filters(manifest, auto_verify)
    assetlinks = render_assetlinks(manifest)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, INTENT_FILTERS_FILENAME),
              "w", encoding="utf-8") as fh:
        fh.write(xml)
    with open(os.path.join(out_dir, ASSETLINKS_FILENAME),
              "w", encoding="utf-8") as fh:
        fh.write(assetlinks)
    return {"intentFiltersXml": xml, "assetlinksJson": assetlinks}


def _expected(manifest_path):
    manifest = load_manifest(manifest_path)
    auto_verify = validate_structure(manifest)
    return {
        INTENT_FILTERS_FILENAME:
            render_intent_filters(manifest, auto_verify),
        ASSETLINKS_FILENAME: render_assetlinks(manifest),
    }


def check(manifest_path, out_dir):
    """Compare checked-in outputs against fresh generation.

    Returns 0 on match, 2 on drift (unified diff printed to stdout).
    Raises CodegenError(SCHEMA_MISMATCH) for bad manifests.
    """
    expected = _expected(manifest_path)
    drifted = False
    for filename, want in expected.items():
        path = os.path.join(out_dir, filename)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                have = fh.read()
        except OSError:
            have = ""
        if have != want:
            drifted = True
            diff = difflib.unified_diff(
                have.splitlines(keepends=True),
                want.splitlines(keepends=True),
                fromfile="checked-in %s" % filename,
                tofile="generated %s" % filename,
            )
            sys.stdout.write("--- drift: %s\n" % filename)
            sys.stdout.writelines(diff)
            if not have.endswith("\n") or not want.endswith("\n"):
                sys.stdout.write("\n")
    return 2 if drifted else 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="FR-NAV-006 T2 App Links codegen")
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--check", action="store_true",
                        help="compare checked-in outputs, exit 2 on drift")
    args = parser.parse_args(argv)
    try:
        if args.check:
            return check(args.manifest, args.out)
        generate(args.manifest, args.out)
        return 0
    except CodegenError as exc:
        sys.stderr.write("CodegenError{reason: %s, detail: %s}\n"
                         % (exc.reason, exc.detail))
        return 1


if __name__ == "__main__":
    sys.exit(main())
