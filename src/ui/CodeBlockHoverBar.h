#pragma once

#include "common/SyntaxHighlighter.h"
#include <windows.h>
#include <functional>
#include <string>

namespace anynote::ui {

struct CodeBlockInfo;

class CodeBlockHoverBar {
public:
    using LanguageChangedCallback = std::function<void(common::CodeLanguage)>;
    using CopyClickedCallback = std::function<void()>;

    CodeBlockHoverBar() = default;
    ~CodeBlockHoverBar();

    bool Initialize(HWND hParent);
    HWND GetHwnd() const noexcept { return m_hWnd; }

    void AttachToCodeBlock(const CodeBlockInfo& info, const RECT& rcBlockInParent);
    void UpdatePosition(const RECT& rcBlockInParent);
    void Hide();

    bool IsVisible() const;
    bool IsMouseOver() const;
    bool IsInteracting() const { return m_languageMenuOpen || IsMouseOver(); }
    long GetActiveTableStart() const noexcept { return m_activeTableStart; }

    void SetOnLanguageChanged(LanguageChangedCallback cb) { m_onLangChanged = std::move(cb); }
    void SetOnCopyClicked(CopyClickedCallback cb) { m_onCopyClicked = std::move(cb); }

    void ResetCopiedState();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Paint(HDC hdc);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseLeave();

    int CalculateWidth(UINT dpi) const;

    HWND m_hWnd = nullptr;
    HWND m_hParent = nullptr;

    long m_activeTableStart = -1;
    common::CodeLanguage m_currentLang = common::CodeLanguage::PlainText;
    bool m_isCopied = false;
    bool m_hoverLang = false;
    bool m_hoverCopy = false;
    bool m_languageMenuOpen = false;

    LanguageChangedCallback m_onLangChanged;
    CopyClickedCallback m_onCopyClicked;

    static inline bool s_classRegistered = false;
};

} // namespace anynote::ui
