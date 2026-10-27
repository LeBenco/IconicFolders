// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#pragma once
#include <windows.h>
#include <string>
namespace iconic {
std::wstring tr(UINT id, HMODULE module = nullptr);
std::wstring format(UINT id, const std::wstring& value);
void selectLanguage();
}
