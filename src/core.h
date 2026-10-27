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
struct IconInfo { std::wstring resource; fs::path source; bool managed = false; };
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
IconInfo inspect(const fs::path& folder);
bool needsOverwrite(const fs::path& folder, const fs::path& source, Mode mode);
void apply(const fs::path& folder, const fs::path& source, Mode mode, bool overwriteApproved);
void reset(const fs::path& folder, bool deleteApproved);
void notify(const fs::path& folder);
std::wstring quoteArg(const std::wstring&);
}
