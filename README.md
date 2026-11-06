<img src="assets/icon.png" alt="IconicFolders icon" width="64" align="right">

# IconicFolders

Change folder icons from the Windows context menu, and restore the default icon
at any time. For Windows 10 and later (x64), in French or English.

[Download the installer](https://github.com/LeBenco/IconicFolders/releases/latest).

## Usage

1. Install IconicFolders for yourself or all users.
2. Right-click one or more folders and choose **Folder icon → Choose an icon…**.
3. Choose a source:

   - **Image or icon:** select an ICO, PNG, JPEG or BMP file.
   - **Another folder:** reuse its icon.
   - **Embedded icons:** browse Windows collections or add an EXE/DLL.

   Click **Apply**, or double-click an icon to apply it.

On Windows 11, open **Show more options** to find the menu.

If the folder is on a network or removable drive, or is copied to another computer,
its custom icon will not appear if the original icon file is unavailable there.
To address this, IconicFolders offers two modes:

- **Use the original icon:** use the selected file directly. It must remain
  accessible at the same path.
- **Copy the icon into the folder:** store a hidden copy inside the folder and
  use it as the folder’s icon. This copy travels with the folder.

This choice is available only for `.ico` files. For other sources:

- **PNG, JPEG and BMP images:** converted and copied automatically, centered
  without cropping.
- **EXE/DLL icons:** used by reference only.

Choose **Folder icon → Restore default icon** to reset.

Changing or restoring a folder’s icon affects only the folders you select.
Their subfolders keep their existing icons.

## Updates

Run the new installer over the existing installation, even while its DLL is loaded.
Restart Explorer when offered, or later, to activate context-menu changes.
Uninstalling leaves customized folders intact.

If repeated early Explorer crashes disable the extension, open `IconicFolders.exe`
to re-enable it after installing a corrected version.

## Technical details

Folder icons use Windows’ `desktop.ini` mechanism. Its `IconResource` entry points
to the original file in reference mode, or to a relative path inside the folder
in copy mode. Other settings and formatting are preserved, and the required file
and folder attributes are applied automatically.

### Build

Built in C++20 with Visual Studio 2022 C++ Build Tools, Windows SDK 10.0.19041+
and bundled Microsoft WIL headers. The build uses PowerShell; installer generation
requires NSIS 3 and Inkscape with Python/Pillow. ImageMagick is also needed when
regenerating the application icons.

```powershell
.\build.ps1 -Configuration Release -Installer
```

## License

Copyright (C) 2026 Benco. **GPL-3.0-only**; see [LICENSE](LICENSE).
[Microsoft WIL](external/wil/LICENSE) and the adapted
[Fluent Emoji pencil](assets/third-party/LICENSE-Microsoft-Fluent-Emoji.txt)
retain their MIT licenses and notices.
