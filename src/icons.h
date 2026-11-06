// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#pragma once
#include "core.h"
#include <wil/resource.h>
namespace iconic {
bool imageFile(const fs::path&);
bool iconFile(const fs::path&);
std::vector<int> resourceIcons(const fs::path&);
IconData loadIconData(const fs::path&, int index = 0);
IconData folderIconData(const fs::path&);
wil::unique_hicon previewIcon(const IconData&, int pixels);
}
