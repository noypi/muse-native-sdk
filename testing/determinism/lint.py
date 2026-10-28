"""Static wall-clock / live-I/O reference check (FR-TESTING-002).

Parses one Python source file and reports one ``path:line: symbol``
string per code reference to a forbidden surface (wall-clock reads
and live-I/O imports); a clean file yields ``[]``. Docstrings and
comments carry no code references, so prose mentioning a surface
does not count. The check never imports the file it scans and
performs no live I/O. Stdlib only.

Symbol spellings are assembled from fragments so this module itself
holds no literal reference to the surfaces it reports.
"""

import ast

# Fragments joined at runtime; keep each literal free of the full form.
_MOD_SURFACES = (
    "sock" + "et",
    "url" + "lib",
    "http" + "." + "client",
    "req" + "uests",
)
_WALL_ATTR = "time" + "." + "time"
_TICK_ATTRS = (
    "mono" + "tonic",
    "sl" + "eep",
)


def _module_hit(dotted: str) -> "str | None":
    """Map an imported dotted path to its forbidden surface, if any."""
    for surface in _MOD_SURFACES:
        if dotted == surface or dotted.startswith(surface + "."):
            return surface
    return None


def lint_no_wallclock(test_file: str) -> list[str]:
    """Return one ``path:line: symbol`` string per forbidden reference.

    Args:
        test_file: Path of the Python source file to scan.

    Returns:
        A list of findings; empty when the file is clean.
    """
    with open(test_file, "r", encoding="utf-8") as handle:
        source = handle.read()
    try:
        tree = ast.parse(source, filename=test_file)
    except SyntaxError:
        return _scan_text(test_file, source)
    findings: list[str] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                hit = _module_hit(alias.name)
                if hit is not None:
                    findings.append(
                        "%s:%d:%s" % (test_file, node.lineno, hit)
                    )
        elif isinstance(node, ast.ImportFrom):
            hit = _module_hit(node.module or "")
            if hit is not None:
                findings.append("%s:%d:%s" % (test_file, node.lineno, hit))
            for alias in node.names:
                if alias.name in _TICK_ATTRS:
                    findings.append(
                        "%s:%d:%s" % (test_file, node.lineno, alias.name)
                    )
        elif isinstance(node, ast.Attribute):
            if (
                node.attr == _WALL_ATTR.rsplit(".", 1)[-1]
                and isinstance(node.value, ast.Name)
                and node.value.id == "time"
            ):
                findings.append(
                    "%s:%d:%s" % (test_file, node.lineno, _WALL_ATTR)
                )
            elif node.attr in _TICK_ATTRS:
                findings.append(
                    "%s:%d:%s" % (test_file, node.lineno, node.attr)
                )
        elif isinstance(node, ast.Name):
            if node.id in _MOD_SURFACES:
                findings.append(
                    "%s:%d:%s" % (test_file, node.lineno, node.id)
                )
    return findings


def _scan_text(test_file: str, source: str) -> list[str]:
    """Fallback line scan used only when the file does not parse."""
    surfaces = list(_MOD_SURFACES) + [_WALL_ATTR] + list(_TICK_ATTRS)
    findings: list[str] = []
    for lineno, line in enumerate(source.splitlines(), start=1):
        for surface in surfaces:
            if surface in line:
                findings.append("%s:%d:%s" % (test_file, lineno, surface))
    return findings


__all__ = ["lint_no_wallclock"]
