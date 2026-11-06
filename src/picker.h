// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#pragma once
#include "icons.h"
namespace iconic {
bool pickIcon(const std::vector<fs::path>& folders, IconData&, Mode& effective, Mode& preferred);
void rememberIconChoice(const fs::path&, Mode preferred);
}
