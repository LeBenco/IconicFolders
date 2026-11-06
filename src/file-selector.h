// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#pragma once
#include <windows.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace iconic {
enum class SelectorKind { ImageOrIcon, Directory };
struct FileSelection {
    std::filesystem::path path;
    bool directory=false;
    bool operator==(const FileSelection&) const = default;
};
struct SelectorOptions {
    HWND owner=nullptr;
    UINT notification=0;
    UINT_PTR tag=0;
    SelectorKind kind=SelectorKind::ImageOrIcon;
    std::filesystem::path initialFolder,initialSelection;
    RECT bounds={};
};
struct SelectorEvent {
    enum class Type { Ready, Selection, Navigation, Activate, Cancel, Failed, Stopped } type;
    std::optional<FileSelection> selection;
    std::filesystem::path folder;
    HRESULT error=S_OK;
};
// UI-thread contract. No Shell COM interfaces, child-dialog IDs or icon handling
// cross this boundary. Events are owned values; notification is only a wake-up
// (wParam = tag). Always drain takeEvents(), including while closing.
// requestStop() is asynchronous: keep pumping messages until stopped(), then
// destroy the selector before its owner window. Its HWND remains valid meanwhile.
class IFileSelector {
public:
    virtual ~IFileSelector()=default;
    virtual HWND window() const noexcept=0;
    virtual void setBounds(const RECT&,UINT dpi)=0;
    virtual void show(bool visible)=0;
    // Ask the native picker to validate its current filename/selection. A
    // successful validation produces Activate and closes the native dialog.
    // Keep pumping until stopped() before destroying or replacing the selector.
    virtual void activateSelection()=0;
    virtual SIZE preferredSize(UINT dpi) const noexcept=0;
    virtual std::vector<SelectorEvent> takeEvents()=0;
    virtual void requestStop() noexcept=0;
    virtual bool stopped() const noexcept=0;
};
// All experimental IFileDialog hosting and the autonomous fallback live here.
// Only one hosted selector may be running for an owner at a time. Stop the
// previous one before showing another: reparenting shares their input queues.
// A documented IExplorerBrowser backend can replace this factory independently.
// allowEmbedding=false selects the standalone fallback without scheduling the
// hosted dialog (for capability policy or diagnostics).
std::unique_ptr<IFileSelector> createNativeFileSelector(const SelectorOptions&, bool allowEmbedding=true);
}
