"""APK/AAB size checker (compressed, ICU, modules, installed).

Hermetic: stdlib zipfile only, no build artifact required. The caller
supplies the artifact path; anything missing or unparseable raises
SizeError carrying ``.path``/``.reason`` (fail closed, never crashes).

Size envelope (plan A4, AC-009): base compressed APK 12-18 MB, ICU
subset ~3 MB, dynamic modules 15-25 MB, full-stack installed 35-50 MB.
Numeric envelope conformance is asserted by the T8 gate against the
manifest; this checker reports measured sizes from the artifact.
"""

import os
import zipfile
from dataclasses import dataclass

# Maximum accepted path length; anything longer is rejected fail-closed.
_MAX_PATH_LEN = 4096

_MB = 1000.0 * 1000.0


@dataclass
class SizeError(Exception):
    path: str
    reason: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return f"size check failed for artifact {self.path!r}: {self.reason}"


@dataclass
class SizeReport:
    apk_mb: float
    icu_mb: float
    modules_mb: float
    installed_mb: float


def _is_module_entry(name: str) -> bool:
    lowered = name.lower()
    return ("split" in lowered or "dynamic" in lowered
            or "module" in lowered or "feature" in lowered)


def check_size(aab_path: str) -> SizeReport:
    """Measure the size breakdown of the AAB artifact at ``aab_path``.

    Raises SizeError carrying ``.path``/``.reason`` when the artifact is
    missing, empty, unreadable, or not a valid Android App Bundle (a zip
    archive) — including path-traversal labels that resolve to nothing.
    """
    if not isinstance(aab_path, str) or not aab_path:
        raise SizeError(aab_path if isinstance(aab_path, str) else str(aab_path),
                        f"invalid artifact path: {aab_path!r}")
    if len(aab_path) > _MAX_PATH_LEN:
        raise SizeError(aab_path,
                        f"artifact path too long: {len(aab_path)} chars")
    if not os.path.isfile(aab_path):
        raise SizeError(aab_path, f"artifact not found: {aab_path!r}")
    if not zipfile.is_zipfile(aab_path):
        raise SizeError(aab_path, f"not a valid app bundle: {aab_path!r}")
    try:
        apk_mb = os.path.getsize(aab_path) / _MB
        with zipfile.ZipFile(aab_path) as bundle:
            infos = bundle.infolist()
            if not infos:
                raise ValueError("empty app bundle archive")
            icu_bytes = sum(i.compress_size for i in infos
                            if "icu" in i.filename.lower())
            modules_bytes = sum(i.compress_size for i in infos
                                if _is_module_entry(i.filename))
            installed_bytes = sum(i.file_size for i in infos)
    except SizeError:
        raise
    except Exception as exc:
        raise SizeError(aab_path, f"unreadable app bundle: {exc}") from exc
    return SizeReport(
        apk_mb=apk_mb,
        icu_mb=icu_bytes / _MB,
        modules_mb=modules_bytes / _MB,
        installed_mb=installed_bytes / _MB,
    )
