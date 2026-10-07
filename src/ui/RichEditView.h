#pragma once

#include "common/SyntaxHighlighter.h"
#include <windows.h>
#include <richedit.h>
#include <string>
#include <string_view>

namespace anynote::ui {

class RichEditView {
public:
    RichEditView() = default;
    ~RichEditView();

    bool Initialize(HWND hParent, int x, int y, int width, int height, UINT controlId);
    HWND GetHwnd() const noexcept { return m_hWnd; }

    void SetBounds(int x, int y, int width, int height, bool repaint = true);

    // 文本与 RTF 流导入导出
    void SetText(const std::wstring& text);
    std::wstring GetText() const;
    std::wstring GetSelectedText() const;

    bool StreamInRTF(std::string_view rtfData);
    bool StreamInSelectionRTF(std::string_view rtfData);
    std::string StreamOutRTF() const;

    // 富文本样式操作
    void ToggleBold();
    void ToggleItalic();
    void ToggleUnderline();
    void ToggleStrike();

    void SetTextColor(COLORREF color);
    void SetHighlightColor(COLORREF color);

    void InsertBulletList();
    void InsertNumberedList();

    // 核心特色：插入美化且带语法高亮的代码框 (CodeBox)
    bool InsertCodeBlock(std::wstring_view codeContent = L"", common::CodeLanguage lang = common::CodeLanguage::Cpp);

    // 剪贴板与编辑
    void Undo();
    void Redo();
    void Cut();
    void Copy();
    void Paste();
    void SelectAll();

private:
    HWND m_hWnd = nullptr;
    HMODULE m_richEditModule = nullptr;

    void ApplyDefaultFormatting(bool allDocument = false);
};

} // namespace anynote::ui
