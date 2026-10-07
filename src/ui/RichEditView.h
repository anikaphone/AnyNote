#pragma once

#include "common/SyntaxHighlighter.h"
#include <windows.h>
#include <richedit.h>
#include <string>
#include <string_view>
#include <vector>

namespace anynote::ui {

inline constexpr UINT WM_EDITOR_FORMAT_CHANGED = WM_APP + 20;

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
    void ApplyHeading(int level);
    int GetCurrentHeadingLevel() const;

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

    // 表格操作 (插入 3*2 表格、行列调整、删除、单元格导航)
    bool InsertTable(int rows = 2, int cols = 3);
    bool IsCursorInTable() const;
    bool InsertTableRow(bool below = true);
    bool InsertTableColumn(bool right = true);
    bool DeleteTableRow();
    bool DeleteTableColumn();
    bool DeleteTable();
    bool NavigateTableCell(bool forward = true);

    // 剪贴板与编辑
    void Undo();
    void Redo();
    void Cut();
    void Copy();
    void Paste();
    void SelectAll();

    // 文本查找、替换与高亮标记
    std::wstring GetPlainText() const;
    bool FindAndSelect(const std::wstring& text, bool forward = true, bool matchCase = false, bool wholeWord = false, bool wrapAround = true);
    int CountMatches(const std::wstring& text, bool matchCase = false, bool wholeWord = false) const;
    bool SelectRange(LONG start, LONG end);
    bool ReplaceCurrent(const std::wstring& findText, const std::wstring& replaceText, bool forward = true, bool matchCase = false, bool wholeWord = false, bool wrapAround = true);
    int ReplaceAll(const std::wstring& findText, const std::wstring& replaceText, bool matchCase = false, bool wholeWord = false);
    int MarkAll(const std::wstring& text, bool matchCase = false, bool wholeWord = false);
    void ClearMarks();
    bool HasActiveMarks() const noexcept { return !m_markedRanges.empty(); }

private:
    HWND m_hWnd = nullptr;
    HMODULE m_richEditModule = nullptr;
    std::vector<CHARRANGE> m_markedRanges;
    std::vector<CHARFORMAT2W> m_markedFormats;

    void ApplyDefaultFormatting(bool allDocument = false);
};

} // namespace anynote::ui
