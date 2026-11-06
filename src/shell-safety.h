// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#pragma once
#include <windows.h>

namespace iconic {
// Only Explorer's COM activation is gated. DllMain stays an empty loader stub.
bool allowShellActivation(HMODULE module) noexcept;
bool shellSafetyDisabled() noexcept;
HRESULT resetShellSafety() noexcept;
HRESULT watchExplorer(HANDLE inheritedProcess,bool& newlyDisabled) noexcept;

struct ShellFailureState {
    DWORD failures=0;
    ULONGLONG lastFailure=0;
    DWORD generation=0;
    bool disabled=false;
};
constexpr DWORD ShellFailureLimit=5;
constexpr DWORD ShellHealthMilliseconds=10000;
constexpr ULONGLONG ShellFailureWindow=60ULL*10000000ULL;
bool abnormalShellExit(DWORD code) noexcept;
bool recordShellFailure(ShellFailureState& state,ULONGLONG now,DWORD generation) noexcept;
}
