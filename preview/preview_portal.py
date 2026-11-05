"""Render a standalone phone mockup from the same editable portal HTML.

The mockup is a single self-contained HTML file (portal-preview.html) that can be
opened directly in a browser or an isolated Edge profile to check the phone view.

Usage:
    python preview_portal.py
    python preview_portal.py --portal ../firmware/application/rt-thread/helloworld/netapp/portal.html
"""

import argparse
from html import escape
from pathlib import Path

here = Path(__file__).resolve().parent
default_portal = (here.parent / "firmware" / "application" / "rt-thread"
                  / "helloworld" / "netapp" / "portal.html")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--portal", type=Path, default=default_portal,
                        help="editable portal.html (default: firmware app copy)")
    parser.add_argument("--out", type=Path, default=here / "portal-preview.html")
    args = parser.parse_args()

    src = args.portal.read_text(encoding="utf-8")
    names = ("123456789", "Home-WiFi", "Guest-2.4G")
    items = ["<p class='scan-note'>已发现 3 个网络，点选后填写密码</p>"]
    for name in names:
        safe = escape(name, quote=True)
        items.append("<button class='network' type='button' data-ssid=\"%s\" "
                     "onclick='chooseNetwork(this)' aria-pressed='false'>"
                     "<span class='wifi-glyph'>&#x2301;</span>"
                     "<span class='network-name'>%s</span>"
                     "<span class='network-arrow'>&#x203A;</span></button>" % (safe, safe))

    args.out.write_text(src.replace("<!--NETWORKS-->", "".join(items)), encoding="utf-8")
    print(args.out)


if __name__ == "__main__":
    main()
