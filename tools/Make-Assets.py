# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Benco
"""Build Windows icon assets with optical folder masters and the original pencil."""
import argparse
from copy import deepcopy
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid
import xml.etree.ElementTree as ET

from PIL import Image

NS = '{http://www.w3.org/2000/svg}'
ET.register_namespace('', NS[1:-1])
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
# left, right, top, shoulder y, front y, bottom, radius, shoulder x, tab x.
# These optical masters are intentionally tuned for each small Windows size.
FOLDER_MASTERS = {
    16: (1, 14, 2, 4, 5, 14, 1, 8, 11),
    20: (1, 18, 3, 5, 6, 18, 1, 9, 14),
    24: (2, 22, 3, 5, 7, 21, 2, 11, 16),
    32: (2, 29, 4, 7, 10, 28, 2, 15, 22),
    40: (3, 36, 5, 9, 12, 35, 2, 18, 27),
    48: (3, 43, 6, 11, 15, 42, 3, 22, 33),
}


def folder_document(artwork, size):
    """Keep the SVG gradients, but place the folder edges on the optical grid."""
    root = ET.Element(NS + 'svg', width=str(size), height=str(size),
                      viewBox=f'0 0 {size} {size}', fill='none')
    definitions = deepcopy(artwork.find(NS + 'defs'))
    for gradient in definitions:
        if gradient.get('id') in ('folder-back', 'folder-front'):
            for name in ('x1', 'x2', 'y1', 'y2'):
                gradient.set(name, f'{float(gradient.get(name)) * size / 256:.9g}')
    root.append(definitions)
    left, right, top, shoulder, front, bottom, radius, sx, tab = FOLDER_MASTERS[size]
    clip = ET.SubElement(definitions, NS + 'clipPath', id='back-union',
                         clipPathUnits='userSpaceOnUse')
    ET.SubElement(clip, NS + 'rect', x=str(left), y=str(shoulder),
                  width=str(right-left), height=str(front+radius-shoulder), rx=str(radius))
    ET.SubElement(clip, NS + 'rect', x=str(tab), y=str(top),
                  width=str(right-tab), height=str(front+radius-top), rx=str(radius))
    ET.SubElement(clip, NS + 'polygon', points=' '.join(
        f'{x},{y}' for x, y in ((sx, shoulder), (tab, top), (right-radius, top),
                              (right-radius, front+radius), (sx, front+radius))))
    ET.SubElement(root, NS + 'rect', width=str(size), height=str(size),
                  fill='url(#folder-back)', attrib={'clip-path': 'url(#back-union)'})
    ET.SubElement(root, NS + 'rect', x=str(left), y=str(front), width=str(right-left),
                  height=str(bottom-front), rx=str(radius), fill='url(#folder-front)')
    return root


def generate(source, output, work_dir, inkscape):
    artwork = ET.parse(source).getroot()
    if artwork.get('viewBox') != '0 0 256 256':
        raise ValueError('The optical masters require the 256 x 256 source SVG.')
    pencil = artwork.find(NS + 'g')
    if pencil is None or len(pencil) != 5:
        raise ValueError('The source SVG must contain the original five pencil paths.')
    output.parent.mkdir(parents=True, exist_ok=True)
    work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='icon-', dir=work_dir) as temporary:
        temporary = Path(temporary)
        pencil_svg = ET.Element(NS + 'svg', width='256', height='256',
                                viewBox='0 0 256 256', fill='none')
        pencil_svg.extend((deepcopy(artwork.find(NS + 'defs')), deepcopy(pencil)))
        pencil_path = temporary / 'pencil.svg'
        ET.ElementTree(pencil_svg).write(pencil_path, encoding='utf-8')
        renders = [(source, temporary / 'full.png', 1024),
                   (pencil_path, temporary / 'pencil.png', 1920)]
        for size in FOLDER_MASTERS:
            svg = temporary / f'folder-{size}.svg'
            ET.ElementTree(folder_document(artwork, size)).write(svg, encoding='utf-8')
            renders.append((svg, temporary / f'folder-{size}.png', size * 32))
        actions = []
        for svg, png, size in renders:
            actions.extend((f'file-open:{svg.as_posix()}', f'export-filename:{png.as_posix()}',
                            'export-type:png', 'export-area-page', 'export-background-opacity:0',
                            f'export-width:{size}', f'export-height:{size}', 'export-do', 'file-close'))
        subprocess.run([inkscape, '--batch-process', '--actions=' + ';'.join(actions)],
                       check=True, timeout=180,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        with Image.open(temporary / 'full.png') as image:
            full = image.convert('RGBA')
        with Image.open(temporary / 'pencil.png') as image:
            pencil_image = image.convert('RGBA')
        frames = []
        for size in SIZES:
            if size in FOLDER_MASTERS:
                with Image.open(temporary / f'folder-{size}.png') as image:
                    folder = image.convert('RGBA').resize((size, size), Image.Resampling.BOX)
                image = Image.alpha_composite(
                    folder, pencil_image.resize((size, size), Image.Resampling.BOX))
            else:
                image = full.resize((size, size), Image.Resampling.LANCZOS)
            png = temporary / f'icon-{size}.png'
            image.save(png)
            frames.append(png)
        generated = temporary / 'icon.ico'
        subprocess.run(['magick', *map(str, frames), '-define',
                        'icon:png-compression-size=256', str(generated)],
                       check=True, timeout=180,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        with Image.open(generated) as icon:
            if icon.ico.sizes() != {(size, size) for size in SIZES}:
                raise ValueError('The ICO must contain all ten expected sizes.')
            for size, png in zip(SIZES, frames):
                with Image.open(png) as image:
                    expected = image.convert('RGBA')
                actual = icon.ico.getimage((size, size)).convert('RGBA')
                if actual.tobytes() != expected.tobytes():
                    raise ValueError(f'The ICO changed RGBA pixels at {size}px.')
        exports = {output.name: generated}
        for name, size in (('icon.png', 256), ('logo44.png', 44), ('logo150.png', 150)):
            png = temporary / name
            full.resize((size, size), Image.Resampling.LANCZOS).save(png)
            exports[name] = png
        for name, generated in exports.items():
            # Inherit the asset directory's ACL, rather than the private temp ACL.
            destination = output.parent / name
            staged = destination.with_name(f'.{name}.{uuid.uuid4().hex}.tmp')
            try:
                shutil.copyfile(generated, staged)
                staged.replace(destination)
            finally:
                staged.unlink(missing_ok=True)
    print(f'Generated {output}: ten sizes, RGBA pixels verified; PNG exports updated.')


if __name__ == '__main__':
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=root / 'assets/icon.svg')
    parser.add_argument('--output', type=Path, default=root / 'assets/icon.ico')
    parser.add_argument('--work-dir', type=Path, default=root / 'build/assets')
    parser.add_argument('--inkscape', default=shutil.which('inkscape.com') or
                        'C:/Program Files/Inkscape/bin/inkscape.com')
    args = parser.parse_args()
    generate(args.source.resolve(), args.output.resolve(), args.work_dir.resolve(), args.inkscape)
