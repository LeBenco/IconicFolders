<img src="assets/icon.png" alt="IconicFolders icon" width="64" align="right">

# IconicFolders

Change a folder’s icon directly from its Windows context menu, and restore the
default icon at any time. Supports Windows 10 and later (x64), in French or English.

[Download the Windows installer](https://github.com/LeBenco/IconicFolders/releases/latest).

## Usage

1. Install IconicFolders.
2. Right-click one or more folders and choose **Folder icon → Choose an icon…**.
3. Select an `.ico` file, choose how to use it, and click **Apply**.

On Windows 11, open **Show more options** to find the menu.

- **Use the original file:** it must stay available at the same location.
- **Copy into the folder:** a hidden copy stays with the folder when you move it.

To reset, choose **Folder icon → Restore default icon**. Subfolders are not changed.
Uninstalling the app leaves customized folders intact.

## Technical details

Built with C++20, Win32, Shell COM APIs and Microsoft WIL. The context-menu
extension launches a separate app for the icon picker and file operations;
there is no permanent background process.

Folder icons use the standard Windows `desktop.ini` mechanism. `IconResource`
points to the original file or a hidden `iconicfolders.ico` copy inside the folder.
Other folder settings are preserved, and Explorer is notified after changes.

Operations run with the current user's permissions, without elevation. The app
stores only the last source directory and selected mode in
`HKCU\Software\IconicFolders`.

### Build and tests

Requires Visual Studio 2022 C++ Build Tools, Windows SDK 10.0.19041 or newer,
PowerShell and NSIS 3.

```powershell
.\build.ps1 -Configuration Release -Test -Installer
```

If icon assets need regeneration, Inkscape (with bundled Python/Pillow) and
ImageMagick are also required. See [assets/SOURCES.md](assets/SOURCES.md).

## License

Copyright (C) 2026 Benco. Licensed under **GPL-3.0-only**. See [LICENSE](LICENSE).

Third-party components: [Microsoft WIL](external/wil/LICENSE) and a modified
[Microsoft Fluent Emoji pencil](assets/SOURCES.md), both under the MIT license.
