#pragma once

#include "common/SyntaxHighlighter.h"
#include "ui/CodeBlockHoverBar.h"
#include "ui/TableEditor.h"
#include <windows.h>
#include <richedit.h>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace anynote::ui {

inline constexpr UINT WM_EDITOR_FORMAT_CHANGED = WM_APP + 20;
inline constexpr UINT WM_CODEBLOCK_COPIED = WM_APP + 25;
inline constexpr UINT WM_CODEBLOCK_LANG_CHANGED = WM_APP + 26;

struct CodeBlockInfo {
    bool isCodeBlock = false;
    long tableStart = 0;
    long tableEnd = 0;
    long codeStart = 0;
    long codeEnd = 0;
    common::CodeLanguage currentLang = common::CodeLanguage::PlainText;
    std::wstring codeText;
};

struct OutlineItem {
    int level = 0;          // 1 ~ 4
    std::wstring text;      // 标题文本
    LONG charPos = 0;       // 字符起始偏移
    LONG lineIndex = 0;     // 所在行号

    bool operator==(const OutlineItem& other) const = default;
};

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
    std::optional<std::string> StreamOutRTF() const;

    // 富文本样式操作
    void ApplyHeading(int level);
    int GetCurrentHeadingLevel() const;
    std::vector<OutlineItem> ExtractOutlineItems() const;
    void ScrollToCharPos(LONG charPos);

    void ToggleBold();
    void ToggleItalic();
    void ToggleUnderline();
    void ToggleStrike();

    void SetTextColor(COLORREF color);
    void SetHighlightColor(COLORREF color);

    void InsertBulletList();
    void InsertNumberedList();

    // 本地图片插入 (支持 PNG/JPG/BMP/GIF/WEBP 等，自适应宽度等比高清缩放)
    bool InsertImageFromFile(const std::wstring& filePath);

    // 核心特色：插入美化且带语法高亮的代码框 (CodeBox)
    bool InsertCodeBlock(std::wstring_view codeContent = L"", common::CodeLanguage lang = common::CodeLanguage::PlainText);

    // 代码卡片交互支持 (顶栏复制、切换语言、光标检测)
    bool GetCodeBlockAtCursor(CodeBlockInfo* outInfo = nullptr) const;
    bool GetCodeBlockAt(long charPos, CodeBlockInfo* outInfo = nullptr) const;
    bool CopyCodeBlockText(const CodeBlockInfo& info) const;
    bool SwitchCodeBlockLanguage(const CodeBlockInfo& info, common::CodeLanguage newLang);

    // 悬浮条与光标/鼠标交互
    void OnMouseMove(long charPos, POINT ptClient);
    void OnMouseLeave();
    void OnSelChange();
    void UpdateHoverBarPosition();
    RECT GetCodeBlockRect(const CodeBlockInfo& info) const;
    void RefreshCodeBlockLayout();
    void PaintCodeBlockFrames();

    // 表格操作 (插入 3*2 表格、行列调整、删除、单元格导航)
    using TableAlignmentScope = anynote::ui::TableAlignmentScope;

    bool InsertTable(int rows = 2, int cols = 3);
    bool IsCursorInTable() const;
    bool InsertTableRow(bool below = true);
    bool InsertTableColumn(bool right = true);
    bool DeleteTableRow();
    bool DeleteTableColumn();
    bool DeleteTable();
    bool NavigateTableCell(bool forward = true);
    bool SetTableCellHorizontalAlignment(int horzAlign, TableAlignmentScope scope = TableAlignmentScope::Selection);
    bool SetTableCellVerticalAlignment(int vertAlign, TableAlignmentScope scope = TableAlignmentScope::Selection);
    bool GetTableCellAlignment(int* pHorzAlign, int* pVertAlign) const;
    bool SelectTableCells(TableAlignmentScope scope) { return m_tableEditor.Select(scope); }
    size_t GetSelectedTableCellCount() const { return m_tableEditor.SelectionCount(); }
    bool HasTableSelection() const { return m_tableEditor.HasSelection(); }
    void ClearTableSelection() { m_tableEditor.ClearSelection(); }
    bool HandleTableMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);
    bool IsPointInTableSelection(POINT point) const { return m_tableEditor.ContainsPoint(point); }
    void PaintTableSelection() const { m_tableEditor.PaintSelection(); }

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
    bool ReplaceCurrent(const std::wstring& findText, const std::wstring& replaceText, bool forward = true, bool matchCase = false, bool wholeWord = false, bool wrapAround = true, bool* outFoundNext = nullptr);
    int ReplaceAll(const std::wstring& findText, const std::wstring& replaceText, bool matchCase = false, bool wholeWord = false);
    int MarkAll(const std::wstring& text, bool matchCase = false, bool wholeWord = false);
    void ClearMarks();
    bool HasActiveMarks() const noexcept { return !m_markedRanges.empty(); }

private:
    HWND m_hWnd = nullptr;
    HMODULE m_richEditModule = nullptr;
    CodeBlockHoverBar m_hoverBar;
    TableEditor m_tableEditor;
    std::vector<CHARRANGE> m_markedRanges;
    std::vector<CHARFORMAT2W> m_markedFormats;

    void ApplyDefaultFormatting(bool allDocument = false);
    void ApplyCodeTypingFormat();
    bool m_updatingCodeLayout = false;
};

} // namespace anynote::ui
