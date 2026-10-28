#!/usr/bin/env python3
"""Typed-binding code generator for the net codec pipeline (T7).

Reads one schema file and emits a typed C++ struct plus a TypeScript
interface per message/table found:

    codegen.py --schema <file.proto|file.fbs|schema.json> \
        --out-cpp <dir> --out-ts <dir>

Supported schema shapes (minimal, dependency-free):
  * .proto  — `message Point { int32 x = 1; int32 y = 2; }`
  * .fbs    — `table Point { x:int; y:int; }` (or `struct`)
  * .json   — `{"messages": [{"name": "Point", "fields": [
                 {"name": "x", "type": "int32"},
                 {"name": "y", "type": "int32"}]}]}`
              (also accepts a top-level list of such messages).

Each message yields `<name>.h` under --out-cpp containing
`struct <Name> { ... };` and `<name>.ts` under --out-ts containing
`interface <Name> { ... }`. Exits non-zero with a message on stderr
when the schema has no messages or a flag is missing.
"""

import argparse
import json
import os
import re
import sys

PROTO_MSG = re.compile(r"message\s+(\w+)\s*\{(.*?)\}", re.DOTALL)
PROTO_FIELD = re.compile(r"(?:repeated\s+)?(\w+)\s+(\w+)\s*=\s*\d+")
FBS_TABLE = re.compile(r"(?:table|struct)\s+(\w+)\s*\{(.*?)\}", re.DOTALL)
FBS_FIELD = re.compile(r"(\w+)\s*:\s*(\w+)")

CPP_TYPE = {
    "int32": "int32_t",
    "int64": "int64_t",
    "uint32": "uint32_t",
    "uint64": "uint64_t",
    "sint32": "int32_t",
    "sint64": "int64_t",
    "bool": "bool",
    "float": "float",
    "double": "double",
    "string": "std::string",
    "bytes": "std::string",
    "int": "int32_t",
    "long": "int64_t",
    "short": "int16_t",
    "byte": "int8_t",
    "ubyte": "uint8_t",
}

TS_TYPE = {
    "int32": "number",
    "int64": "number",
    "uint32": "number",
    "uint64": "number",
    "sint32": "number",
    "sint64": "number",
    "bool": "boolean",
    "float": "number",
    "double": "number",
    "string": "string",
    "bytes": "string",
    "int": "number",
    "long": "number",
    "short": "number",
    "byte": "number",
    "ubyte": "number",
}


def parse_proto(text):
    messages = []
    for m in PROTO_MSG.finditer(text):
        fields = [
            (ftype, fname)
            for ftype, fname in PROTO_FIELD.findall(m.group(2))
        ]
        if fields:
            messages.append((m.group(1), fields))
    return messages


def parse_fbs(text):
    messages = []
    for m in FBS_TABLE.finditer(text):
        fields = [
            (fname, ftype)
            for fname, ftype in FBS_FIELD.findall(m.group(2))
        ]
        if fields:
            # normalize to (type, name) pairs
            messages.append((m.group(1), [(t, n) for n, t in fields]))
    return messages


def parse_json_schema(text):
    data = json.loads(text)
    if isinstance(data, dict) and "messages" in data:
        data = data["messages"]
    if isinstance(data, dict):
        data = [data]
    messages = []
    for entry in data:
        name = entry.get("name")
        fields = [
            (f.get("type", "string"), f.get("name", "field"))
            for f in entry.get("fields", [])
        ]
        if name and fields:
            messages.append((name, fields))
    return messages


def cpp_for(name, fields):
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <string>",
        "",
        "struct %s {" % name,
    ]
    for ftype, fname in fields:
        lines.append("  %s %s;" % (CPP_TYPE.get(ftype, "std::string"), fname))
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def ts_for(name, fields):
    lines = ["interface %s {" % name]
    for ftype, fname in fields:
        lines.append("  %s: %s;" % (fname, TS_TYPE.get(ftype, "string")))
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Emit typed C++/TS bindings for a message schema.")
    ap.add_argument("--schema", required=True,
                    help="schema file (.proto, .fbs, or .json)")
    ap.add_argument("--out-cpp", required=True, help="C++ output directory")
    ap.add_argument("--out-ts", required=True,
                    help="TypeScript output directory")
    args = ap.parse_args(argv)

    with open(args.schema, "r", encoding="utf-8") as fh:
        text = fh.read()

    lower = args.schema.lower()
    if lower.endswith(".fbs"):
        messages = parse_fbs(text)
    elif lower.endswith(".json"):
        messages = parse_json_schema(text)
    else:
        messages = parse_proto(text)
        if not messages:
            messages = parse_fbs(text)

    if not messages:
        print("codegen: no messages found in %s" % args.schema,
              file=sys.stderr)
        return 1

    os.makedirs(args.out_cpp, exist_ok=True)
    os.makedirs(args.out_ts, exist_ok=True)
    for name, fields in messages:
        base = name.lower()
        with open(os.path.join(args.out_cpp, base + ".h"), "w",
                  encoding="utf-8") as fh:
            fh.write(cpp_for(name, fields))
        with open(os.path.join(args.out_ts, base + ".ts"), "w",
                  encoding="utf-8") as fh:
            fh.write(ts_for(name, fields))
    return 0


if __name__ == "__main__":
    sys.exit(main())
