#!/usr/bin/env python3
"""Assembles tools/theme-editor.html from tools/editor/editor.html: puts in the
same glyphs sampleboot-ui draws with (src/font_data.h), the test images
(tools/editor/images.json) and the original settings (card/sampleboot.conf), so
the page is a single self-contained file. Standard library only."""
import base64, json, re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
header = (ROOT / "src" / "font_data.h").read_text()
fonts = {}
for short, name in (("small", "font_small"), ("label", "font_label"), ("name", "font_name"), ("big", "font_big")):
    px = bytes(int(x) for x in re.search(r"static const uint8_t %s_px\[\] = \{([^}]*)\}" % name, header).group(1).split(","))
    g = [[int(v) for v in t.split(",")] for t in
         re.findall(r"\{(-?\d+,-?\d+,-?\d+,-?\d+,-?\d+,-?\d+,-?\d+)\}",
                    re.search(r"static const glyph_t %s_g\[\] = \{(.*?)\};" % name, header, re.S).group(1))]
    m = re.search(r"static const font_t %s = \{(\d+),(\d+)," % name.upper(), header)
    fonts[short] = {"ascent": int(m.group(1)), "height": int(m.group(2)), "g": g,
                    "px": base64.b64encode(px).decode()}
images = json.loads((ROOT / "tools" / "editor" / "images.json").read_text())
conf = (ROOT / "card" / "sampleboot.conf").read_text()
splashes = {n: "data:image/png;base64," + base64.b64encode((ROOT / "card" / f"splash-{n}.png").read_bytes()).decode()
            for n in ("dark", "light")}
logo = (ROOT / "docs" / "images" / "logo-light.svg").read_text()
logo = logo[logo.index("<svg"):].replace('width="100%" height="100%" ', 'role="img" aria-label="SampleBoot: one card, every OS" ', 1)
page = (ROOT / "tools" / "editor" / "editor.html").read_text()
page = page.replace("/*FONTS*/null", json.dumps(fonts, separators=(",", ":")))
page = page.replace("/*IMAGES*/null", json.dumps(images, separators=(",", ":")))
page = page.replace("/*SPLASHES*/null", json.dumps(splashes))
page = page.replace("<!--LOGO-->", logo)
page = page.replace('/*CONF*/""', json.dumps(conf))
out = ROOT / "tools" / "theme-editor.html"
out.write_text(page)
print(f"{out} ({len(page) // 1024} KB)")
