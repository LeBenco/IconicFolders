<img src="assets/icon.png" alt="IconicFolders icon" width="64" align="right">

# IconicFolders

IconicFolders lets you change a folder’s icon directly from its context menu.
Choose an icon, decide whether to use the original file or keep a copy in the
folder, and apply the change. You can restore the default icon at any time.

The app runs on Windows 10 and later (x64), in French or English according to
your Windows language.

[Download the Windows installer](https://github.com/LeBenco/IconicFolders/releases/latest).

## Usage

1. Right-click a folder and choose **Folder icon → Choose an icon…**.
2. Select an `.ico` file.
3. Choose whether to use the original icon or copy it into the folder.
4. Click **Apply**.

Using the original keeps the folder linked to that file: it must remain available
at the same location. Copying the icon into the folder keeps it with the folder
when you move it to another location or computer. The copy is hidden.

To remove a custom icon, choose **Folder icon → Restore default icon**. The
original icon file is kept. If the app copied the icon into the folder, it asks
before deleting that copy. It also asks before replacing an existing copy.

You can select several folders and apply the same icon to all of them in one go.
Their subfolders are not changed. Cancelling leaves everything as it was.

The picker remembers the last source folder used successfully. If a folder uses
an original icon file, the picker opens at that file’s location when available.
For a copied icon, it uses the last source folder; the copy’s origin is not tracked.

On Windows 11, open **Show more options** to find the menu.

## Technical details

### What changes in desktop.ini

IconicFolders uses the standard Windows `desktop.ini` customization mechanism.
The chosen icon is written as `IconResource` in the `[.ShellClassInfo]` section.
For an original icon, this is an absolute path:

```ini
[.ShellClassInfo]
IconResource=C:\Icons\Archive.ico,0
```

For a copied icon, it is a relative reference to the hidden file
`iconicfolders.ico` in the customized folder:

```ini
[.ShellClassInfo]
IconResource=iconicfolders.ico,0
```

Only icon entries are changed. Duplicate `IconResource` entries and the older
`IconFile`/`IconIndex` entries are removed to avoid conflicting definitions.
All other settings, comments, blank lines, spacing and section order are retained.

A managed copy is recognized by the exact relative reference
`IconResource=iconicfolders.ico,0`. An absolute reference to a file with that same
name is still an original file and is never deleted on reset. Other icon files
are preserved. Do not use this reserved relative reference for unrelated files.

The existing encoding is preserved. Like the native Windows INI reader, the app
supports UTF-16 LE with BOM and the Windows ANSI code page
without BOM. UTF-8 BOM and UTF-16 BE are refused without modifying the file:
silently rewriting the entire file would violate preservation. An ANSI file that
cannot represent a new path is left intact; use copy mode in that case. New INI
files are UTF-16 LE with BOM. Hidden/system flags are applied to `desktop.ini`,
and the folder receives the read-only Shell customization flag. Other attributes
are retained. Reset preserves the INI and folder flags because other Shell
customizations may use them.

### App and preferences

The app uses C++20, Win32 and the Shell COM APIs, with WIL for resource management.
The context-menu DLL starts a separate executable for the file picker and file
operations. There is no permanent background process and no per-folder database.

The last successfully applied source directory and mode are the only preferences
(`HKCU\Software\IconicFolders`). Cancel leaves these and the target files unchanged.
Existing references open at their source directory when accessible. Copies use
the global last directory; their source is intentionally not tracked.

Command line: `IconicFolders.exe --choose <folder> […]` or `--reset <folder> […]`.
Launching without arguments displays usage. Operations use the current user's
permissions and never request elevation. Links/reparse points on the target or
managed files are rejected. Errors for a batch are grouped in one dialog.

## Build and tests

Requires Visual Studio 2022 C++ Build Tools, Windows SDK 10.0.19041 or newer,
PowerShell and NSIS 3 for the installer. WIL is vendored and pinned; no restore.

Icon exports are checked in. When `assets/icon.svg` or `tools/Make-Assets.py`
changes, the build regenerates them with Inkscape's bundled Python/Pillow and
ImageMagick. No separate Python installation is needed. `INKSCAPE` and
`ICON_PYTHON` can override the default installation paths. Use `-RegenerateIcons`
to force regeneration. Small folder shapes use manual optical masters; see
[assets/SOURCES.md](assets/SOURCES.md) for the rendering method.

```powershell
.\build.ps1 -Configuration Release -Test -Installer
.\tools\Install-ASan.ps1
.\build.ps1 -Configuration Asan -Test
.\tools\Test-Installer.ps1
.\tools\Test-Installer.ps1 -LockedDll
```

The installer test refuses to run over existing IconicFolders settings and uses
an isolated folder, then removes its registration. It activates the installed COM
server and invokes the real helper on two Unicode folder names.

The ASan script installs official Microsoft packages matching MSVC 14.44 locally,
verified against SHA-256 from the installed VS catalog. It avoids changing the
shared VS installation. Instrumented DLLs are tested in a separate test process;
they are never installed into Explorer. A separate intentional overflow verifies
that ASan detects errors; it is not shipped. Release uses a static CRT and has no
ASan dependency. Tests create and clean their own data under `build/`.

The NSIS installer installs for the current user without UAC, creates the context
menu and Windows uninstall entry, and adds no desktop shortcut. Uninstall removes
program settings and registration, leaving customized folders intact. If Explorer
still holds the DLL, one current-user RunOnce value retries removal at the next
sign-in and removes itself. This requires no administrator rights.

For direct Windows 11 modern-menu integration, build the provided sparse-package
manifest with `tools\Build-IdentityPackage.ps1 -CertificateThumbprint <thumbprint>`.
The certificate subject must match the manifest Publisher, and the certificate
must be trusted on the target machine. Then register with
`Add-AppxPackage -Path <package.msix> -ExternalLocation <install-directory>`.
Remove it with `Get-AppxPackage IconicFolders | Remove-AppxPackage` before uninstall.
The standard unsigned installer uses the classic menu and does not install a
development trust certificate. No production signing certificate is included.

## Languages

Strings are centralized in `src/app.rc`. At launch the Windows user UI language
selects French (including regional variants) or English. Explicit native resource
lookup ensures English fallback: LoadString alone can fall back to another OS
language. Explorer's thread language is never changed. Adding another language
requires one resource block and extending the small language mapping in `ui.cpp`.
Paths, registry keys and managed filenames are stable.

## References

* [Microsoft: desktop.ini](https://learn.microsoft.com/windows/win32/shell/how-to-customize-folders-with-desktop-ini)
* [Microsoft: common file dialog](https://learn.microsoft.com/windows/win32/shell/common-file-dialog)
* [Microsoft: Shell context menu and package identity](https://learn.microsoft.com/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer)
* [Microsoft: AddressSanitizer](https://learn.microsoft.com/cpp/sanitizers/asan)
* [WIL](https://github.com/microsoft/wil)

## License

Copyright (C) 2026 Benco.

IconicFolders is licensed under the **GNU General Public License, version 3 only**
(GPL-3.0-only). See [LICENSE](LICENSE) for the full terms.

The vendored Microsoft WIL headers retain their own MIT license and notices in
[external/wil/LICENSE](external/wil/LICENSE).

The application icon includes a modified Microsoft Fluent Emoji pencil under
the MIT license. Its origin and changes are documented in
[assets/SOURCES.md](assets/SOURCES.md); the
[Microsoft license](assets/third-party/LICENSE-Microsoft-Fluent-Emoji.txt) is also
included in the installer.
