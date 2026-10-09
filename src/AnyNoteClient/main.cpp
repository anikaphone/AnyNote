#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <gdiplus.h>
#include "resource.h"
#include "ui/MainWindow.h"
#include <sqlite3.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int nCmdShow) {
    // 1. 设置 Windows 10/11 Per-Monitor V2 DPI 感知 (保证高分屏 4K 150%/200% 极度锐利清晰)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // RichEdit 的图片粘贴路径依赖 OLE/COM，必须先初始化 GUI 线程的 OLE apartment。
    HRESULT oleResult = OleInitialize(nullptr);
    if (FAILED(oleResult)) {
        MessageBoxW(nullptr, L"初始化 OLE/COM 失败，无法启用富文本剪贴板功能。", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 2. 初始化 GDI+ (用于工具栏 Fluent 矢量图标高质量抗锯齿渲染)
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::Status gdiStatus = Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    if (gdiStatus != Gdiplus::Ok) {
        OleUninitialize();
        MessageBoxW(nullptr, L"初始化 GDI+ 图形引擎失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 3. 初始化 Windows 通用控件库 (Common Controls v6.0)
    INITCOMMONCONTROLSEX icex = {sizeof(INITCOMMONCONTROLSEX)};
    icex.dwICC = ICC_WIN95_CLASSES | ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // 4. RichEdit 5.0 内核在 RichEditView::Initialize 中按需加载

    // 5. 验证 sqlite3mc 静态集成 (调用静态库函数确保编译期链接成功)
    const char* sqliteVer = sqlite3_libversion();
    (void)sqliteVer; // 保留符号引用

    int exitCode = 1;
    {
        // 6. 创建并显示主窗口。确保其子控件在 GdiplusShutdown/OleUninitialize 前销毁。
        anynote::ui::MainWindow mainWindow;
        if (!mainWindow.Initialize(hInstance, nCmdShow)) {
            MessageBoxW(nullptr, L"初始化 AnyNote 主窗口失败！", L"错误", MB_OK | MB_ICONERROR);
        } else {
            // 7. 加载快捷键表
            HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR));

            // 8. Win32 主消息循环
            MSG msg = {};
            while (GetMessageW(&msg, nullptr, 0, 0)) {
                HWND hFindDlg = mainWindow.GetFindReplaceDialogHwnd();
                if (hFindDlg && IsWindow(hFindDlg) && IsWindowVisible(hFindDlg) && IsDialogMessageW(hFindDlg, &msg)) {
                    continue;
                }

                if (!hAccel || !TranslateAcceleratorW(mainWindow.GetHwnd(), hAccel, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            exitCode = static_cast<int>(msg.wParam);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();
    return exitCode;
}

