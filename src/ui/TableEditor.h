#pragma once

#include <windows.h>
#include <richedit.h>
#include <vector>

namespace anynote::ui {

enum class TableAlignmentScope { Selection, Column, Row, All };

// A cell selection is independent of RichEdit's single, contiguous text range.
// Character positions are discarded before any edit that can invalidate them.
class TableEditor {
public:
    void Attach(HWND window) { m_window = window; }
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);
    void ClearSelection();
    void PaintSelection() const;
    bool ContainsPoint(POINT point) const;
    bool Select(TableAlignmentScope scope);
    bool SetAlignment(int value, bool vertical, TableAlignmentScope scope);
    bool GetAlignment(int* horizontal, int* vertical) const;
    size_t SelectionCount() const { return m_selected.size(); }
    bool HasSelection() const { return !m_selected.empty(); }

private:
    friend struct TableEditorGeometryTest;
    struct Cell {
        long rowStart = 0;
        long rowEnd = 0;
        long rowIndex = 0;
        long column = 0;
        long start = 0;
        long end = 0; // Cell terminator, excluded from paragraph formatting.
    };
    bool ReadTable(long position, std::vector<Cell>& cells) const;
    bool Targets(TableAlignmentScope scope, std::vector<Cell>& cells) const;
    bool CellRect(const Cell& cell, RECT& rect) const;
    bool HitTest(POINT point, Cell& cell, std::vector<Cell>* table = nullptr) const;
    void SetCaret(long position);
    void UpdateDrag(POINT point);

    HWND m_window = nullptr;
    std::vector<Cell> m_selected;
    std::vector<Cell> m_dragTable;
    std::vector<Cell> m_dragBase;
    Cell m_anchor;
    POINT m_mouseDown{};
    bool m_tracking = false;
    bool m_dragging = false;
    bool m_control = false;
    bool m_internal = false;


};

} // namespace anynote::ui
