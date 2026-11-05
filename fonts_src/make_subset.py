"""Build fonts_src/_static.ttf and a small GB2312 subset from a Noto Sans SC variable font.

Only needed if you want to rebuild the font source. The repository already ships
fonts_src/_static.ttf, which is the input used by generate_fonts.py.

Get the variable font (SIL Open Font License 1.1) from Google Fonts:
    https://fonts.google.com/noto/specimen/Noto+Sans+SC
    file name: NotoSansSC[wght].ttf

Usage:
    pip install fonttools
    python make_subset.py --var-font "path/to/NotoSansSC[wght].ttf"
"""

import argparse
from pathlib import Path

from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

here = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--var-font", type=Path, required=True,
                        help="NotoSansSC[wght].ttf variable font")
    parser.add_argument("--out-dir", type=Path, default=here)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    static = args.out_dir / "_static.ttf"

    # 1) freeze the variable font at weight 400 -> _static.ttf
    font = TTFont(str(args.var_font))
    font = instantiateVariableFont(font, {"wght": 400}, inplace=False)
    font.save(str(static))
    font.close()

    # 2) GB2312 level-1 hanzi + the ranges the UI can print -> small subset
    chars = set()
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
                if "\u4e00" <= ch <= "\u9fff":
                    chars.add(ch)
            except UnicodeDecodeError:
                pass

    ranges = [(0x20, 0x7E), (0xA0, 0xFF), (0x2010, 0x2027),
              (0x2103, 0x2103), (0x3000, 0x303F), (0xFF00, 0xFFEF)]
    unis = [cp for a, b in ranges for cp in range(a, b + 1)]
    unis.extend(ord(c) for c in chars)

    font = TTFont(str(static))
    subsetter = Subsetter(options=Options())
    subsetter.populate(unicodes=sorted(set(unis)))
    subsetter.subset(font)
    out = args.out_dir / "NotoSansSC-Regular-subset.ttf"
    font.save(str(out))
    print("_static.ttf:", static.stat().st_size, "bytes")
    print("subset:", out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
