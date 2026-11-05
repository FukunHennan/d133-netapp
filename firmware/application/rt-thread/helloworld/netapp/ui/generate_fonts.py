"""Regenerate the small Noto Sans SC fonts used by the handwritten LVGL UI.

It scans ui_app.c (and the desktop preview main.c) for every character that can
appear on screen, then calls lv_font_conv through npx to emit 16/24/36 px C
arrays. The 36 px font is digits + sign only (big sensor readings).

Requirements:
  * Node.js + npm (npx), first run downloads lv_font_conv from the npm registry
  * fonts_src/_static.ttf (included in this repository)

Usage:
    python generate_fonts.py
    python generate_fonts.py --sizes 16,24 --extra-symbols "°CkPa"
    python generate_fonts.py --ttf D:/fonts/_static.ttf --out-dir /tmp/fonts
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent


def find_up(start: Path, rel: str) -> Path | None:
    for base in (start, *start.parents):
        candidate = base / rel
        if candidate.exists():
            return candidate
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ttf", type=Path,
                        default=find_up(here, "fonts_src/_static.ttf"),
                        help="Noto Sans SC static TTF (default: <repo>/fonts_src/_static.ttf)")
    parser.add_argument("--sim-main", type=Path,
                        default=find_up(here, "simulator/main.c") or find_up(here, "lvgl_simulator/main.c"),
                        help="desktop preview main.c whose strings must also have glyphs")
    parser.add_argument("--out-dir", type=Path, default=here,
                        help="where to write ui_font_*.c (default: this directory)")
    parser.add_argument("--sizes", default="16,24,36")
    parser.add_argument("--extra-symbols", default="",
                        help="extra characters for the large font, e.g. \"°CkPaN/A\"")
    args = parser.parse_args()

    if not args.ttf or not Path(args.ttf).exists():
        print("error: Noto Sans SC _static.ttf not found; pass --ttf", file=sys.stderr)
        return 2

    sources = [here / "ui_app.c"]
    if args.sim_main:
        sources.append(Path(args.sim_main))

    # 只扫描字符串字面量：注释里的中文不应进入字库，否则字库会被注释撑大
    def string_literals(path: Path) -> str:
        if not path.exists():
            return ""
        return "".join(m.group(1) for m in
                       re.finditer(r'"((?:[^"\\]|\\.)*)"', path.read_text(encoding="utf-8")))

    text = "".join(string_literals(p) for p in sources)

    # Han, CJK punctuation, full-width forms, degree sign and U+2103 DEGREE CELSIUS
    cjk = "".join(sorted(set(re.findall(
        r"[\u3000-\u303f\u3400-\u9fff\uff00-\uffef\u00b0\u2103]", text))))

    npx = "npx.cmd" if sys.platform == "win32" else "npx"
    failed = False
    for size in (int(s) for s in args.sizes.split(",") if s.strip()):
        name = f"ui_font_{size}"
        if size >= 32:
            symbols = "-.0123456789" + args.extra_symbols
        else:
            symbols = cjk + args.extra_symbols
        cmd = [npx, "--yes", "lv_font_conv", "--font", str(args.ttf),
               "--size", str(size), "--bpp", "4", "--format", "lvgl",
               "--no-compress",
               "--lv-include", "lvgl.h", "--lv-font-name", name,
               "--symbols", symbols, "-o", str(args.out_dir / f"{name}.c")]
        if size < 32:
            cmd.extend(("--range", "0x20-0x7E"))
        print("+", " ".join(cmd))
        if subprocess.run(cmd, check=False).returncode != 0:
            failed = True
            continue
        print(name, (args.out_dir / f"{name}.c").stat().st_size, "bytes")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
