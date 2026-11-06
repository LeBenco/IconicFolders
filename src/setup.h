// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#pragma once
#include <windows.h>
namespace iconic {
using RestartTrace=void(*)(PCWSTR stage,DWORD result) noexcept;
HRESULT restartExplorer(bool probeOnly=false,RestartTrace trace=nullptr,bool forceAfterTimeout=true) noexcept;
}
