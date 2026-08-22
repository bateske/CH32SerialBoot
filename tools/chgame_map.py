"""Parse bootloader/src/chgame_map.h so host tools never hard-code the layout.

The C header is the single source of truth; this evaluates the handful of
simple #defines we need rather than duplicating the numbers in Python.
"""
import pathlib
import re

_HEADER = pathlib.Path(__file__).resolve().parent.parent / "bootloader" / "src" / "chgame_map.h"
_DEFINE = re.compile(r"^#define\s+(CHGAME_\w+)\s+(.+?)\s*(?:/\*.*)?$")


def load(path=None):
    """Return {name: int} for the CHGAME_* object-like macros in chgame_map.h."""
    text = pathlib.Path(path or _HEADER).read_text()
    values = {}
    for line in text.splitlines():
        m = _DEFINE.match(line.strip())
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        if name.endswith(("_H",)) or "(" in name:
            continue
        expr = re.sub(r"/\*.*?\*/", "", expr).strip().rstrip("u")
        expr = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)u\b", r"\1", expr)
        try:
            values[name] = int(eval(expr, {"__builtins__": {}}, dict(values)))  # noqa: S307
        except Exception:
            continue  # struct typedefs, string macros, anything non-numeric
    return values


MAP = load()

FLASH_SIZE   = MAP["CHGAME_FLASH_SIZE"]
PAGE_SIZE    = MAP["CHGAME_PAGE_SIZE"]
BOOT_SIZE    = MAP["CHGAME_BOOT_SIZE"]
APP_START    = MAP["CHGAME_APP_START"]
APP_MAX_SIZE = MAP["CHGAME_APP_MAX_SIZE"]
META_ADDR    = MAP["CHGAME_META_ADDR"]
META_MAGIC   = MAP["CHGAME_META_MAGIC"]
META_VERSION = MAP["CHGAME_META_VERSION"]
BOOT_MAGIC   = MAP["CHGAME_BOOT_MAGIC"]

if __name__ == "__main__":
    import sys as _sys
    # Single-value query for shell scripts: --boot-size, --app-start, --page-size ...
    if len(_sys.argv) > 1 and _sys.argv[1].startswith("--"):
        _key = "CHGAME_" + _sys.argv[1][2:].upper().replace("-", "_")
        if _key not in MAP:
            raise SystemExit("unknown key " + _key)
        print(MAP[_key])
    else:
        for k, v in sorted(MAP.items()):
            print(f"{k:24} 0x{v:08X}  {v}")
