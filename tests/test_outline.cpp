#include "ui/RichEditView.h"
#include "ui/OutlinePane.h"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>

using anynote::ui::RichEditView;
using anynote::ui::OutlinePane;
using anynote::ui::OutlineItem;

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

LRESULT CALLBACK DummyHostWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace

int main() {
    OleInitialize(nullptr);
    InitCommonControls();

    int result = 0;
    HWND hHost = nullptr;

    try {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = DummyHostWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"TestOutlineHost";
        RegisterClassExW(&wc);

        hHost = CreateWindowExW(
            0, L"TestOutlineHost", L"Test Outline Host",
            WS_OVERLAPPEDWINDOW, 100, 100, 800, 600,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
        );
        Check(hHost != nullptr, "Cannot create dummy host window");

        // 1. 测试 RichEditView 的标题应用与大纲提取
        RichEditView editor;
        Check(editor.Initialize(hHost, 0, 0, 500, 500, 1001), "Cannot initialize RichEditView");

        // 输入一段富文本内容：含 H1, 正文, H2, H3, 正文, H4, 跳级 H2
        editor.SetText(L"第一章 介绍\r\n这是第一章的正文内容。\r\n\r\n1.1 背景\r\n1.1.1 历史演进\r\n演进正文。\r\n1.1.1.1 细节标注\r\n第二章 核心设计\r\n");

        // 将各段落应用对应级别标题格式
        // 段落 1: "第一章 介绍" -> H1
        editor.SelectRange(0, 5);
        editor.ApplyHeading(1);
        Check(editor.GetCurrentHeadingLevel() == 1, "Paragraph 1 must be heading 1");

        // 段落 2: "这是第一章的正文内容。" -> 正文 (Heading 0)
        editor.SelectRange(10, 15);
        Check(editor.GetCurrentHeadingLevel() == 0, "Paragraph 2 must be body text");

        // 段落 4: "1.1 背景" -> H2
        LONG posH2 = static_cast<LONG>(editor.GetText().find(L"1.1 背景"));
        Check(posH2 != -1, "Cannot find '1.1 背景'");
        editor.SelectRange(posH2, posH2 + 4);
        editor.ApplyHeading(2);
        Check(editor.GetCurrentHeadingLevel() == 2, "1.1 背景 must be heading 2");

        // 段落 5: "1.1.1 历史演进" -> H3
        LONG posH3 = static_cast<LONG>(editor.GetText().find(L"1.1.1 历史演进"));
        Check(posH3 != -1, "Cannot find '1.1.1 历史演进'");
        editor.SelectRange(posH3, posH3 + 6);
        editor.ApplyHeading(3);
        Check(editor.GetCurrentHeadingLevel() == 3, "1.1.1 历史演进 must be heading 3");

        // 段落 7: "1.1.1.1 细节标注" -> H4
        LONG posH4 = static_cast<LONG>(editor.GetText().find(L"1.1.1.1 细节标注"));
        Check(posH4 != -1, "Cannot find '1.1.1.1 细节标注'");
        editor.SelectRange(posH4, posH4 + 8);
        editor.ApplyHeading(4);
        Check(editor.GetCurrentHeadingLevel() == 4, "1.1.1.1 细节标注 must be heading 4");

        // 段落 8: "第二章 核心设计" -> H1
        LONG posH1_2 = static_cast<LONG>(editor.GetText().find(L"第二章 核心设计"));
        Check(posH1_2 != -1, "Cannot find '第二章 核心设计'");
        editor.SelectRange(posH1_2, posH1_2 + 6);
        editor.ApplyHeading(1);
        Check(editor.GetCurrentHeadingLevel() == 1, "第二章 核心设计 must be heading 1");

        // 提取大纲项
        std::vector<OutlineItem> items = editor.ExtractOutlineItems();
        Check(items.size() == 5, "ExtractOutlineItems must extract exactly 5 headings");

        Check(items[0].level == 1 && items[0].text == L"第一章 介绍", "Item 0 mismatch");
        Check(items[1].level == 2 && items[1].text == L"1.1 背景", "Item 1 mismatch");
        Check(items[2].level == 3 && items[2].text == L"1.1.1 历史演进", "Item 2 mismatch");
        Check(items[3].level == 4 && items[3].text == L"1.1.1.1 细节标注", "Item 3 mismatch");
        Check(items[4].level == 1 && items[4].text == L"第二章 核心设计", "Item 4 mismatch");

        std::cout << "[PASS] RichEditView heading formatting and outline extraction passed.\n";

        // 2. 测试 OutlinePane 控件
        OutlinePane outlinePane;
        Check(outlinePane.Initialize(hHost), "Cannot initialize OutlinePane");
        outlinePane.SetBounds(500, 0, 250, 500, true);

        LONG clickedPos = -1;
        outlinePane.SetOnItemSelected([&](LONG charPos) {
            clickedPos = charPos;
        });

        // 填充大纲项到树形控件
        outlinePane.SetOutlineItems(items);

        // 验证双向定位高亮：选择不同正文位置
        // 在第一章正文内 (pos < posH2)
        outlinePane.SelectNearestItem(items[0].charPos + 5);
        // 在 1.1 背景内部
        outlinePane.SelectNearestItem(items[1].charPos + 2);
        // 在 1.1.1.1 细节标注内部
        outlinePane.SelectNearestItem(items[3].charPos + 3);
        // 在第二章内部
        outlinePane.SelectNearestItem(items[4].charPos + 2);

        // 验证内容脏检查：折叠节点后再次调用 SetOutlineItems(items) 不被重新强行展开
        HWND hTree = FindWindowExW(outlinePane.GetHwnd(), nullptr, WC_TREEVIEWW, nullptr);
        Check(hTree != nullptr, "Outline tree handle must be valid");
        HTREEITEM hRoot = TreeView_GetRoot(hTree);
        Check(hRoot != nullptr, "Must have root tree item");
        // 折叠根节点
        TreeView_Expand(hTree, hRoot, TVE_COLLAPSE);
        UINT state = TreeView_GetItemState(hTree, hRoot, TVIS_EXPANDED);
        Check((state & TVIS_EXPANDED) == 0, "Root item must be collapsed");

        // 再次调用相同 items（模拟失焦或点击时 UpdateOutline），验证脏检查阻止了重新 ExpandAll
        outlinePane.SetOutlineItems(items);
        state = TreeView_GetItemState(hTree, hRoot, TVIS_EXPANDED);
        Check((state & TVIS_EXPANDED) == 0, "Root item must remain collapsed due to dirty-check");

        // 测试空大纲
        outlinePane.ClearOutline();
        outlinePane.SetOutlineItems({});

        std::cout << "[PASS] OutlinePane hierarchy building and sync navigation passed.\n";

        // 3. 测试自动换行 (Word Wrap) 功能及正文/代码块/表格排他性
        {
            HWND hEdit = editor.GetHwnd();
            // 验证默认状态与接口查询
            editor.SetWordWrap(true);
            Check(editor.IsWordWrap(), "IsWordWrap must return true after SetWordWrap(true)");

            // (1) 正文测试：开启换行时自动折行，关闭换行时不折行
            std::wstring longText = L"这是一个很长很长的正文测试段落。";
            for (int i = 0; i < 15; ++i) {
                longText += L"这是第" + std::to_wstring(i) + L"句用于测试自动折行的长内容，在窗口有限宽度下必须能够正常被自动换行。";
            }
            editor.SetText(longText);

            LRESULT linesWrapOn = SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
            Check(linesWrapOn > 1, "Long text must wrap into multiple lines when WordWrap is ON");

            editor.SetWordWrap(false);
            Check(!editor.IsWordWrap(), "IsWordWrap must return false after SetWordWrap(false)");
            LRESULT linesWrapOff = SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
            Check(linesWrapOff == 1, "Long text must stay in a single line when WordWrap is OFF");

            // (2) 标题测试：标题同样适用自动换行
            editor.SelectRange(0, static_cast<LONG>(longText.size()));
            editor.ApplyHeading(1);
            editor.SetWordWrap(true);
            LRESULT h1WrapOn = SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
            Check(h1WrapOn > 1, "Heading 1 must wrap into multiple lines when WordWrap is ON");

            editor.SetWordWrap(false);
            LRESULT h1WrapOff = SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
            Check(h1WrapOff == 1, "Heading 1 must stay in a single line when WordWrap is OFF");

            // (3) 代码块排他性测试：开启自动换行时，代码块内容坚决不折行
            editor.SetWordWrap(true);
            editor.SetText(L"Normal heading or text\n");
            editor.SelectRange(23, 23);
            std::wstring longCodeLine = L"const char* long_sql_query = \"SELECT id, name, description, created_at, updated_at FROM anynote_very_long_table_name_that_must_not_wrap_under_any_circumstances_in_code_block\";";
            Check(editor.InsertCodeBlock(longCodeLine, anynote::common::CodeLanguage::Cpp), "InsertCodeBlock failed");

            anynote::ui::CodeBlockInfo codeInfo;
            Check(editor.GetCodeBlockAtCursor(&codeInfo), "Cannot get inserted code block");

            // 检查代码行内的字符坐标：长单行代码的首尾字符应该处于同一行高度（Y 坐标相同）
            POINT ptStart{}, ptLong{};
            SendMessageW(hEdit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptStart), static_cast<LPARAM>(codeInfo.codeStart + 5));
            SendMessageW(hEdit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptLong), static_cast<LPARAM>(codeInfo.codeStart + 120));
            Check(ptStart.y == ptLong.y, "Code block line must NOT wrap even when WordWrap is ON");

            // (3b) 代码块中文回退与超长 Tab 制表符排他性测试（避免字符猜测误差导致折行）
            // 用例 A：120 个中文字符单行
            editor.SetText(L"Header\n");
            editor.SelectRange(7, 7);
            std::wstring chineseComment = L"// ";
            for (int i = 0; i < 120; ++i) chineseComment += L"中";
            Check(editor.InsertCodeBlock(chineseComment, anynote::common::CodeLanguage::Cpp), "InsertCodeBlock chinese failed");
            anynote::ui::CodeBlockInfo chineseInfo;
            Check(editor.GetCodeBlockAtCursor(&chineseInfo), "Cannot get inserted chinese code block");
            POINT ptZhStart{}, ptZhEnd{};
            SendMessageW(hEdit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptZhStart), static_cast<LPARAM>(chineseInfo.codeStart + 5));
            SendMessageW(hEdit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptZhEnd), static_cast<LPARAM>(chineseInfo.codeStart + 115));
            Check(ptZhStart.y == ptZhEnd.y, "Code block with 120 Chinese characters must NOT wrap");

            // 用例 B：含 40 个 Tab 的单行
            editor.SetText(L"Header\n");
            editor.SelectRange(7, 7);
            std::wstring tabLine = L"int val = ";
            for (int i = 0; i < 40; ++i) tabLine += L"\t";
            tabLine += L"100;";
            Check(editor.InsertCodeBlock(tabLine, anynote::common::CodeLanguage::Cpp), "InsertCodeBlock tab failed");
            anynote::ui::CodeBlockInfo tabInfo;
            Check(editor.GetCodeBlockAtCursor(&tabInfo), "Cannot get inserted tab code block");
            POINT ptTabStart{}, ptTabEnd{};
            SendMessageW(hEdit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptTabStart), static_cast<LPARAM>(tabInfo.codeStart + 2));
            SendMessageW(hEdit, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptTabEnd), static_cast<LPARAM>(tabInfo.codeStart + tabLine.size() - 1));
            Check(ptTabStart.y == ptTabEnd.y, "Code block with 40 tabs must NOT wrap");

            // (4) 表格排他性测试：插入表格并验证切换不影响表格几何结构
            editor.SetText(L"\n");
            editor.SelectRange(0, 0);
            Check(editor.InsertTable(2, 3), "InsertTable failed");
            Check(editor.IsCursorInTable(), "Cursor should be inside table");
            editor.SetWordWrap(false);
            editor.SetWordWrap(true);
            Check(editor.IsCursorInTable(), "Table structure preserved across WordWrap toggle");

            std::cout << "[PASS] WordWrap toggle and code block / table exclusion tests passed.\n";
        }

        std::cout << "[ALL OUTLINE TESTS PASSED SUCCESSFULLY!]\n";

    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << "\n";
        result = 1;
    }

    if (hHost) {
        DestroyWindow(hHost);
    }
    OleUninitialize();
    return result;
}
