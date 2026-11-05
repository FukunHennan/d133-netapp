"""Embed the editable phone portal HTML as a small C header.

Usage (from anywhere):
    python generate_portal.py                 # writes portal_html.h next to this script
    python generate_portal.py --out ../build/portal_html.h
"""

import argparse
import json
from pathlib import Path

here = Path(__file__).resolve().parent


def c_literal(text: str) -> str:
    return "\n".join("    " + json.dumps(line, ensure_ascii=False)
                     for line in text.splitlines(keepends=True))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=here / "portal_html.h",
                        help="output C header path (default: portal_html.h next to this script)")
    args = parser.parse_args()

    html = (here / "portal.html").read_text(encoding="utf-8")
    prefix, suffix = html.split("<!--NETWORKS-->")

    out = "/* Generated from portal.html. Edit the HTML and rerun generate_portal.py. */\n"
    out += "static const char portal_prefix[] =\n" + c_literal(prefix) + ";\n"
    out += "static const char portal_suffix[] =\n" + c_literal(suffix) + ";\n"
    for name in ("connecting", "error"):
        content = (here / f"portal_{name}.html").read_text(encoding="utf-8")
        out += f"static const char portal_{name}[] =\n" + c_literal(content) + ";\n"
    ota = (here / "ota.html").read_text(encoding="utf-8")
    out += "static const char ota_page[] =\n" + c_literal(ota) + ";\n"

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(out, encoding="utf-8")
    print("portal_html.h:", args.out.stat().st_size, "bytes ->", args.out)


if __name__ == "__main__":
    main()
