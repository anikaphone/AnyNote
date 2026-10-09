#include "TableEditor.h"
#include <algorithm>
#include <string>
#include <tom.h>
#include <windowsx.h>
#include <wrl/client.h>

namespace anynote::ui {
namespace {
using Microsoft::WRL::ComPtr;

ComPtr<ITextDocument2> Document(HWND window) {
    ComPtr<IUnknown> ole;
    SendMessageW(window, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(ole.GetAddressOf()));
    ComPtr<ITextDocument2> document;
    if (ole) ole.As(&document);
    return document;
}

// Group a format operation into one undo step; don't move the visible caret.
class EditCollection {
public:
    EditCollection(ITextDocument2* document, HWND window) : m_document(document), m_window(window) {
        SendMessageW(window, EM_STOPGROUPTYPING, 0, 0);
        m_document->BeginEditCollection();
    }
    ~EditCollection() {
        m_document->EndEditCollection();
        InvalidateRect(m_window, nullptr, FALSE);
    }
private:
    ITextDocument2* m_document;
    HWND m_window;
};
}

bool TableEditor::ReadTable(long position, std::vector<Cell>& cells) const {
    auto document = Document(m_window);
    ComPtr<ITextRange2> range;
    if (!document || FAILED(document->Range2(position, position, &range))) return false;
    long delta = 0, start = 0;
    if (FAILED(range->Expand(tomTable, &delta)) || delta == 0) return false;
    range->GetStart(&start);
    BSTR raw = nullptr;
    if (FAILED(range->GetText(&raw))) return false;
    const std::wstring text(raw ? raw : L"", SysStringLen(raw));
    SysFreeString(raw);
    cells.clear();
    // RichEdit exposes row-start/end markers and a BEL cell terminator through
    // TOM. Only skip the structural CR after a row marker, not empty paragraphs.
    long rowIndex = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\xfff9') continue;
        const auto rowEnd = text.find(L'\xfffb', i + 1);
        if (rowEnd == std::wstring::npos) return false;
        const auto nested = text.find(L'\xfff9', i + 1);
        if (nested < rowEnd) return false; // Do not misaddress nested/imported tables.
        size_t cellStart = i + 1;
        if (cellStart < rowEnd && text[cellStart] == L'\r') ++cellStart;
        long column = 0;
        for (size_t j = cellStart; j < rowEnd; ++j) {
            if (text[j] != L'\a') continue;
            cells.push_back({start + static_cast<long>(i), start + static_cast<long>(rowEnd),
                rowIndex, column++, start + static_cast<long>(cellStart), start + static_cast<long>(j)});
            cellStart = j + 1;
        }
        i = rowEnd;
        ++rowIndex;
    }
    return !cells.empty();
}

bool TableEditor::Targets(TableAlignmentScope scope, std::vector<Cell>& targets) const {
    CHARRANGE selection{};
    SendMessageW(m_window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    std::vector<Cell> table;
    if (!ReadTable(selection.cpMin, table)) {
        // A text selection can include prose before the table. Inspect only that
        // range using a detached TOM range, never expand the live selection.
        auto document = Document(m_window);
        ComPtr<ITextRange2> range;
        if (!document || selection.cpMin == selection.cpMax ||
            FAILED(document->Range2(selection.cpMin, selection.cpMax, &range))) return false;
        BSTR raw = nullptr;
        if (FAILED(range->GetText(&raw))) return false;
        const std::wstring text(raw ? raw : L"", SysStringLen(raw));
        SysFreeString(raw);
        const auto marker = text.find(L'\xfff9');
        if (marker == std::wstring::npos ||
            !ReadTable(selection.cpMin + static_cast<long>(marker) + 1, table)) return false;
    }
    std::vector<Cell> selected;
    if (!m_selected.empty()) {
        for (const auto& cell : table) {
            if (std::any_of(m_selected.begin(), m_selected.end(), [&](const Cell& other) {
                return cell.start == other.start;
            })) selected.push_back(cell);
        }
    } else {
        for (const auto& cell : table) {
            const long lower = cell.column == 0 ? cell.rowStart : cell.start;
            const bool intersects = selection.cpMin == selection.cpMax
                ? selection.cpMin >= lower && selection.cpMin <= cell.end
                : selection.cpMin <= cell.end && selection.cpMax > cell.start;
            if (intersects) selected.push_back(cell);
        }
    }
    if (selected.empty()) return false;
    targets.clear();
    for (const auto& cell : table) {
        if (scope == TableAlignmentScope::All ||
            std::any_of(selected.begin(), selected.end(), [&](const Cell& other) {
                if (scope == TableAlignmentScope::Column) return cell.column == other.column;
                if (scope == TableAlignmentScope::Row) return cell.rowStart == other.rowStart;
                return cell.start == other.start;
            })) targets.push_back(cell);
    }
    return !targets.empty();
}

bool TableEditor::CellRect(const Cell& cell, RECT& rect) const {
    auto document = Document(m_window);
    ComPtr<ITextRange2> range;
    ComPtr<ITextRow> row;
    if (!document || FAILED(document->Range2(cell.rowStart, cell.rowStart, &range)) ||
        FAILED(range->GetRow(&row)) || FAILED(row->Reset(tomRowUpdate))) return false;

    long numerator = 0, denominator = 0;
    SendMessageW(m_window, EM_GETZOOM, reinterpret_cast<WPARAM>(&numerator), reinterpret_cast<LPARAM>(&denominator));
    if (numerator <= 0 || denominator <= 0) numerator = denominator = 1;
    const int dpi = GetDpiForWindow(m_window);
    const auto pixels = [&](long twips) { return MulDiv(twips, dpi * numerator, 1440 * denominator); };
    long indent = 0, margin = 0, offset = 0, width = 0, height = 0;
    row->GetIndent(&indent);
    row->GetCellMargin(&margin);
    row->GetHeight(&height);
    for (long column = 0; column <= cell.column; ++column) {
        if (FAILED(row->SetCellIndex(column)) || FAILED(row->GetCellWidth(&width))) return false;
        if (column < cell.column) offset += width;
    }
    long alignment = 0;
    row->GetCellAlignment(&alignment);
    long leftBorder = 0, topBorder = 0, rightBorder = 0, bottomBorder = 0;
    row->GetCellBorderWidths(&leftBorder, &topBorder, &rightBorder, &bottomBorder);
    const int topInset = std::max(1, pixels(topBorder));
    const int bottomInset = std::max(1, pixels(bottomBorder));

    // GetPoint returns text-line bounds, which move with vertical alignment.
    // Measure each cell separately: a row-wide text rectangle mixes offsets
    // from differently aligned cells and cannot represent its physical bounds.
    std::vector<Cell> cells;
    if (!ReadTable(cell.start, cells)) return false;
    long textTop = 0, textBottom = 0;
    float spaceBefore = 0.0f;
    int actualHeight = std::abs(pixels(height));
    for (const auto& other : cells) {
        if (other.rowStart != cell.rowStart) continue;
        ComPtr<ITextRange2> content;
        long x = 0, top = 0, bottom = 0;
        const long flags = tomClientCoord | tomAllowOffClient;
        if (FAILED(document->Range2(other.start, other.end, &content)) ||
            content->GetPoint(tomStart | TA_TOP | flags, &x, &top) != S_OK ||
            content->GetPoint(tomEnd | TA_BOTTOM | flags, &x, &bottom) != S_OK) return false;
        if (height >= 0) actualHeight = std::max(actualHeight, static_cast<int>(bottom - top) + topInset + bottomInset);
        if (other.start == cell.start) {
            textTop = top; textBottom = bottom;
            ComPtr<ITextPara> para;
            if (SUCCEEDED(content->GetPara(&para))) para->GetSpaceBefore(&spaceBefore);
        }
    }
    const int spare = std::max(0, actualHeight - topInset - bottomInset - static_cast<int>(textBottom - textTop));
    const int shift = alignment == 2 ? spare : alignment == 1 ? spare / 2 : 0;
    const long spaceBeforeTwips = static_cast<long>(spaceBefore * 20.0f);
    // RichEdit paints the border on the first device pixel inside the row.
    rect.top = textTop - topInset - pixels(margin) - pixels(spaceBeforeTwips) - shift + 1;
    rect.bottom = rect.top + actualHeight;
    RECT format{};
    POINT scroll{};
    SendMessageW(m_window, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
    SendMessageW(m_window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    rect.left = format.left - scroll.x + pixels(indent + offset);
    rect.right = format.left - scroll.x + pixels(indent + offset + width);
    return rect.right > rect.left && rect.bottom > rect.top;
}
bool TableEditor::HitTest(POINT point, Cell& cell, std::vector<Cell>* table) const {
    POINTL p{point.x, point.y};
    const long position = static_cast<long>(SendMessageW(m_window, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&p)));
    std::vector<Cell> cells;
    if (position >= 0) ReadTable(position, cells);
    if (cells.empty() && !m_selected.empty()) {
        for (const auto& selected : m_selected) {
            RECT rect{};
            if (!CellRect(selected, rect) || !PtInRect(&rect, point)) continue;
            cells = m_selected;
            cell = selected;
            if (table) *table = cells;
            return true;
        }
    }
    if (cells.empty()) return false;
    for (const auto& candidate : cells) {
        RECT rect{};
        if (CellRect(candidate, rect) && PtInRect(&rect, point)) {
            cell = candidate;
            if (table) *table = std::move(cells);
            return true;
        }
    }
    for (const auto& selected : m_selected) {
        RECT rect{};
        if (CellRect(selected, rect) && PtInRect(&rect, point)) {
            cell = selected;
            if (table) *table = cells;
            return true;
        }
    }
    return false;
}

void TableEditor::SetCaret(long position) {
    const bool previous = m_internal;
    m_internal = true;
    CHARRANGE range{position, position};
    SendMessageW(m_window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    m_internal = previous;
}

void TableEditor::ClearSelection() {
    const bool captured = m_tracking && (m_dragging || m_control);
    m_tracking = m_dragging = false;
    m_dragTable.clear();
    m_dragBase.clear();
    if (captured && GetCapture() == m_window) ReleaseCapture();
    if (m_selected.empty()) return;
    m_selected.clear();
    InvalidateRect(m_window, nullptr, FALSE);
}

void TableEditor::UpdateDrag(POINT point) {
    if (!m_tracking) return;
    Cell target;
    bool found = false;
    // Keep the drag in its originating table, even if adjacent tables touch.
    for (const auto& cell : m_dragTable) {
        RECT rect{};
        if (CellRect(cell, rect) && PtInRect(&rect, point)) {
            target = cell;
            found = true;
            break;
        }
    }
    if (!found || (!m_dragging && target.start == m_anchor.start)) return;
    if (!m_dragging && abs(point.x - m_mouseDown.x) < GetSystemMetrics(SM_CXDRAG) &&
        abs(point.y - m_mouseDown.y) < GetSystemMetrics(SM_CYDRAG)) return;
    m_dragging = true;
    SetCapture(m_window);
    m_selected = m_control ? m_dragBase : std::vector<Cell>{};
    for (const auto& cell : m_dragTable) {
        if (cell.rowStart < std::min(m_anchor.rowStart, target.rowStart) ||
            cell.rowStart > std::max(m_anchor.rowStart, target.rowStart) ||
            cell.column < std::min(m_anchor.column, target.column) ||
            cell.column > std::max(m_anchor.column, target.column)) continue;
        if (std::none_of(m_selected.begin(), m_selected.end(), [&](const Cell& other) {
            return other.start == cell.start;
        })) m_selected.push_back(cell);
    }
    SetCaret(m_anchor.start);
    InvalidateRect(m_window, nullptr, FALSE);
}

bool TableEditor::ContainsPoint(POINT point) const {
    return std::any_of(m_selected.begin(), m_selected.end(), [&](const Cell& cell) {
        RECT rect{};
        return CellRect(cell, rect) && PtInRect(&rect, point);
    });
}

bool TableEditor::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (m_internal) return false;
    result = 0;
    switch (message) {
    case WM_LBUTTONDOWN: {
        Cell cell;
        std::vector<Cell> table;
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!HitTest(point, cell, &table)) { ClearSelection(); return false; }
        const bool control = (wParam & MK_CONTROL) != 0;
        if (!control || (!m_selected.empty() &&
            std::none_of(table.begin(), table.end(), [&](const Cell& candidate) {
                return candidate.start == m_selected.front().start;
            }))) ClearSelection();
        m_anchor = cell;
        m_mouseDown = point;
        m_dragTable = std::move(table);
        m_dragBase = m_selected;
        m_tracking = true;
        m_control = control;
        if (control) {
            auto found = std::find_if(m_selected.begin(), m_selected.end(), [&](const Cell& other) {
                return other.start == cell.start;
            });
            if (found == m_selected.end()) m_selected.push_back(cell);
            else m_selected.erase(found);
            SetFocus(m_window);
            SetCaret(m_selected.empty() ? cell.start : m_selected.front().start);
            SetCapture(m_window);
            InvalidateRect(m_window, nullptr, FALSE);
            return true;
        }
        return false; // Within one cell retain native character selection.
    }
    case WM_MOUSEMOVE:
        if (m_tracking && (wParam & MK_LBUTTON)) {
            UpdateDrag({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return m_dragging || m_control;
        }
        break;
    case WM_LBUTTONUP: {
        const bool handled = m_tracking && (m_dragging || m_control);
        m_tracking = m_dragging = false;
        m_dragTable.clear();
        m_dragBase.clear();
        if (handled && GetCapture() == m_window) ReleaseCapture();
        return handled;
    }
    case WM_RBUTTONDOWN:
        if (ContainsPoint({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)})) return true;
        ClearSelection();
        break;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) == m_window) break;
    case WM_CANCELMODE:
        m_tracking = m_dragging = false;
        m_dragTable.clear();
        m_dragBase.clear();
        break;
    case WM_KEYDOWN:
        // Modifier keys alone must not discard a Ctrl multi-selection.
        if (wParam != VK_CONTROL && wParam != VK_SHIFT && wParam != VK_MENU &&
            wParam != VK_APPS && !(wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000))) ClearSelection();
        break;
    case WM_LBUTTONDBLCLK:
    case WM_CHAR:
    case WM_SETTEXT:
    case EM_SETTEXTEX:
    case EM_STREAMIN:
    case EM_REPLACESEL:
    case EM_EXSETSEL:
    case EM_SETSEL:
    case WM_CUT:
    case WM_PASTE:
    case WM_CLEAR:
    case WM_UNDO:
    case EM_UNDO:
    case EM_REDO:
    case EM_PASTESPECIAL:
    case WM_NCDESTROY:
        ClearSelection();
        break;
    }
    return false;
}

void TableEditor::PaintSelection() const {
    if (m_selected.empty()) return;
    HDC dc = GetDC(m_window);
    if (!dc) return;
    const int saved = SaveDC(dc);
    RECT client{};
    GetClientRect(m_window, &client);
    IntersectClipRect(dc, client.left, client.top, client.right, client.bottom);
    // Draw one frame over the native cell border. A second focus rectangle
    // would be inset from the cell and makes the selection look misaligned.
    HBRUSH border = CreateSolidBrush(RGB(50, 115, 200));
    SetBkMode(dc, TRANSPARENT);
    for (const auto& cell : m_selected) {
        RECT rect{};
        if (!CellRect(cell, rect)) continue;
        FrameRect(dc, &rect, border);
    }
    RestoreDC(dc, saved);
    DeleteObject(border);
    ReleaseDC(m_window, dc);
}

bool TableEditor::Select(TableAlignmentScope scope) {
    std::vector<Cell> cells;
    if (!Targets(scope, cells)) return false;
    m_selected = std::move(cells);
    SetCaret(m_selected.front().start);
    InvalidateRect(m_window, nullptr, FALSE);
    SetFocus(m_window);
    return true;
}

bool TableEditor::SetAlignment(int value, bool vertical, TableAlignmentScope scope) {
    if (vertical ? (value < 0 || value > 2) :
        (value != PFA_LEFT && value != PFA_CENTER && value != PFA_RIGHT)) return false;
    std::vector<Cell> targets;
    auto document = Document(m_window);
    if (!document || !Targets(scope, targets)) return false;

    EditCollection edit(document.Get(), m_window);
    bool success = true;
    if (!vertical) {

        CHARRANGE original{};
        POINT scroll{};
        SendMessageW(m_window, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&original));
        SendMessageW(m_window, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        m_internal = true;
        SendMessageW(m_window, WM_SETREDRAW, FALSE, 0);
        for (const auto& cell : targets) {
            // EM_SETPARAFORMAT is the RichEdit path that reliably marks the
            // paragraph dirty and records the operation in the undo stack.
            CHARRANGE selection{cell.start, std::max(cell.start, cell.end)};
            SendMessageW(m_window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
            PARAFORMAT2 format{sizeof(PARAFORMAT2)};
            format.dwMask = PFM_ALIGNMENT;
            format.wAlignment = static_cast<WORD>(value);
            success = SendMessageW(m_window, EM_SETPARAFORMAT, 0,
                reinterpret_cast<LPARAM>(&format)) != 0 && success;
        }
        SendMessageW(m_window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&original));
        SendMessageW(m_window, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        SendMessageW(m_window, WM_SETREDRAW, TRUE, 0);
        m_internal = false;
    } else {

        long previousRow = -1;
        for (const auto& cell : targets) {
            if (cell.rowStart == previousRow) continue;
            previousRow = cell.rowStart;
            ComPtr<ITextRange2> range;
            ComPtr<ITextRow> row;
            if (FAILED(document->Range2(cell.start, cell.start, &range)) ||
                FAILED(range->GetRow(&row)) || FAILED(row->Reset(tomRowUpdate))) { success = false; continue; }
            long height = 0;
            if (FAILED(row->GetHeight(&height))) { success = false; continue; }
            // Positive height is a minimum; preserve taller and exact (negative)
            // imported heights. An automatic one-line row has no alignment space.
            if (height >= 0 && height < 720 && FAILED(row->SetHeight(720))) success = false;
            for (const auto& target : targets) {
                if (target.rowStart != cell.rowStart) continue;
                if (FAILED(row->SetCellIndex(target.column)) || FAILED(row->SetCellAlignment(value))) success = false;
            }
            if (FAILED(row->Apply(1, tomRowApplyDefault))) success = false;
        }
    }
    SetFocus(m_window);
    return success;
}

bool TableEditor::GetAlignment(int* horizontal, int* vertical) const {
    if (horizontal) *horizontal = -1;
    if (vertical) *vertical = -1;
    auto document = Document(m_window);
    std::vector<Cell> targets;
    if (!document || !Targets(TableAlignmentScope::Selection, targets)) return false;
    int commonHorizontal = -1, commonVertical = -1;
    bool first = true;
    for (const auto& cell : targets) {
        ComPtr<ITextRange2> range;
        ComPtr<ITextPara> para;
        ComPtr<ITextRow> row;
        long h = tomUndefined, v = tomUndefined;
        if (FAILED(document->Range2(cell.start, cell.end, &range)) || FAILED(range->GetPara(&para)) ||
            FAILED(para->GetAlignment(&h))) return false;
        ComPtr<ITextRange2> rowRange;
        if (FAILED(document->Range2(cell.rowStart, cell.rowStart, &rowRange)) ||
            FAILED(rowRange->GetRow(&row))) return false;
        row->Reset(tomRowUpdate);
        if (FAILED(row->SetCellIndex(cell.column)) || FAILED(row->GetCellAlignment(&v))) return false;
        const int mapped = h == tomAlignLeft ? PFA_LEFT : h == tomAlignCenter ? PFA_CENTER : h == tomAlignRight ? PFA_RIGHT : -1;
        commonHorizontal = first || commonHorizontal == mapped ? mapped : -1;
        commonVertical = first || commonVertical == v ? static_cast<int>(v) : -1;
        first = false;
    }
    if (horizontal) *horizontal = commonHorizontal;
    if (vertical) *vertical = commonVertical;
    return true;
}

} // namespace anynote::ui
