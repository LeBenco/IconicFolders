// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace iconic {
namespace fs = std::filesystem;
inline constexpr wchar_t ManagedIcon[] = L"iconicfolders.ico";
inline constexpr HRESULT UnsupportedIniEncoding = MAKE_HRESULT(SEVERITY_ERROR,FACILITY_ITF,0x201);
enum class Mode { Reference, Copy };
struct IconInfo { std::wstring resource; fs::path source; bool managed = false; int index = 0; };
struct IconData {
    fs::path source;
    int index = 0;
    bool image = false;
    bool copyAllowed = true;
    UINT width = 0, height = 0;
    std::vector<unsigned char> bytes;
};
// The preference survives an image selection; only the effective mode is forced.
struct IconMode {
    Mode preferred = Mode::Reference;
    Mode effective(bool image, bool copyAllowed = true) const { return image ? Mode::Copy : copyAllowed ? preferred : Mode::Reference; }
    void choose(Mode value, bool image, bool copyAllowed) { if(!image && copyAllowed)preferred=value; }
};
class Ini {
public:
    explicit Ini(const std::vector<unsigned char>& bytes);
    std::wstring icon() const;
    void requireShellEncoding() const;
    std::vector<unsigned char> update(const std::optional<std::wstring>& resource) const;
private:
    enum class Encoding { Ansi, Utf8Bom, Utf16LE, Utf16BE } encoding_;
    std::wstring text_;
    std::wstring encodeValue(const std::wstring&) const;
    std::wstring decodeValue(const std::wstring&) const;
};
std::vector<unsigned char> readFile(const fs::path&, size_t limit = 16 * 1024 * 1024);
void writeFile(const fs::path&, const std::vector<unsigned char>&);
void validateIcon(const fs::path&);
void validateIconBytes(const std::vector<unsigned char>&);
IconInfo inspect(const fs::path& folder);
std::wstring managedIconName(const std::vector<unsigned char>& bytes);
bool needsOverwrite(const fs::path& folder, const IconData& source, Mode mode);
bool needsOverwrite(const fs::path& folder, const fs::path& source, Mode mode);
void apply(const fs::path& folder, const fs::path& source, Mode mode, bool overwriteApproved);
void apply(const fs::path& folder, const IconData& source, Mode mode, bool overwriteApproved);
void reset(const fs::path& folder, bool deleteApproved);
void notify(const fs::path& folder);
std::wstring quoteArg(const std::wstring&);
}
