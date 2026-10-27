# Icon sources

`icon.svg` is the editable source for `icon.ico`, `icon.png`, `logo44.png` and
`logo150.png`. The build regenerates these files when the SVG or generator changes.
It uses Python and Pillow bundled with Inkscape, plus ImageMagick to pack the ICO.
Run `.\build.ps1 -RegenerateIcons` to force regeneration. The `INKSCAPE` and
`ICON_PYTHON` environment variables can override the default Inkscape installation.
The SVG and all exports have transparent backgrounds.

The 16, 20, 24, 32, 40 and 48 px frames use manual folder coordinates in
`tools/Make-Assets.py`. Folder gradients still come from the SVG. The folder and
the unchanged pencil are rendered separately and reduced with a Box filter,
then composited at the target size. These optical masters keep small edges clear
without introducing size-specific changes to the pencil. The 64, 96, 128 and
256 px frames and standalone PNGs use the full SVG rendered at 1024 px and
reduced with Lanczos. The ICO stores 32-bit DIB frames below 256 px and a PNG
frame at 256 px; every decoded RGBA frame is verified before replacing exports.

## Folder

The rounded folder silhouette was drawn for IconicFolders, with the raised tab
on the right. Copyright (C) 2026 Benco, GPL-3.0-only.

The Windows 11 yellow gradient stops were taken as color references from
[Windows 11 FOLDER.svg](https://commons.wikimedia.org/wiki/File:Windows_11_FOLDER.svg),
a vector reconstruction by PantheraLeo1359531 of the Windows 11 23H2 folder icon.
This is a visual reference, not an official Microsoft color specification or a
byte-for-byte export of Windows artwork. The reference is marked PD-shape on
Wikimedia Commons; no folder path was imported from it.

Back: `#FEBE18` to `#EFB122`, horizontal. Front: `#FFE69C`, `#FFE495`,
`#FFDE82`, `#FFD663`, `#FFCA39`, `#FFCA38`, diagonally from the upper left.

## Pencil

The pencil paths are from [Microsoft Fluent Emoji: Pencil, Flat](https://github.com/microsoft/fluentui-emoji/blob/main/assets/Pencil/Flat/pencil_flat.svg).
Copyright (c) Microsoft Corporation, MIT License. The unmodified source is kept
in `third-party/pencil_flat.svg` (upstream Git blob
`e7ad7bfaa8f2b7170329bb239f490390a90d4305`).

For IconicFolders, the pencil was enlarged, with its graphite tip near the center
of the folder front, and its body recolored in two shades of blue. The cone is
white, and the graphite occupies 45% of the
cone's length. The graphite color, metal ferrule and pink eraser retain their
original colors. Its outer silhouette is retained. There is no circular badge
or long background shadow. The full upstream license is in
`third-party/LICENSE-Microsoft-Fluent-Emoji.txt` and is included in the installer.
