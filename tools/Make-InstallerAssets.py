"""FileSpacer wizard artwork layout, using IconicFolders' unmodified app icon."""
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
import argparse
import copy
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image

NS = "{http://www.w3.org/2000/svg}"
ET.register_namespace("", NS[1:-1])


def generate(source, output, inkscape):
    artwork = ET.parse(source).getroot()
    output.mkdir(parents=True, exist_ok=True)
    sidebar = ET.Element(NS + "svg", viewBox="0 0 164 176", width="164", height="176")
    symbol = ET.SubElement(ET.SubElement(sidebar, NS + "defs"), NS + "symbol",
                           id="app", viewBox=artwork.attrib["viewBox"], fill="none")
    for child in artwork:
        if child.tag.startswith(NS) and child.tag != NS + "metadata":
            symbol.append(copy.deepcopy(child))
    ET.SubElement(sidebar, NS + "rect", width="164", height="176", fill="#F1F2F6")
    ET.SubElement(sidebar, NS + "use", href="#app", x="23", y="48", width="118", height="118")
    vector, png = output / "wizard.svg", output / "wizard.png"
    ET.ElementTree(sidebar).write(vector, encoding="utf-8", xml_declaration=True)
    subprocess.run([inkscape, str(vector), f"--export-filename={png}",
                    "--export-width=328", "--export-height=352"], check=True, timeout=180)
    with Image.open(png) as image:
        for language in ("en", "fr"):
            image.convert("RGB").save(output / f"wizard-{language}.bmp")
    for language in ("en", "fr", "pl", "nl", "uk"):
        notes = Path(f"installer/notes-{language}.txt").read_text(encoding="utf-8")
        (output / f"changelog-{language}.txt").write_bytes(
            ("\ufeff" + notes.replace("\r\n", "\n").replace("\n", "\r\n")).encode("utf-16le"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("assets/icon.svg"))
    parser.add_argument("--output", type=Path, default=Path("build/installer-assets"))
    parser.add_argument("--inkscape", required=True)
    args = parser.parse_args()
    generate(args.source.resolve(), args.output.resolve(), args.inkscape)
