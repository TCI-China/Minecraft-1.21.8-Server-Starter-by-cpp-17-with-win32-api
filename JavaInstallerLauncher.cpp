#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

#define ID_DOWNLOAD_BTN 101
#define ID_MESSAGE_STATIC 102

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"JavaInstallerLauncher";
    
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"窗口注册失败!", L"错误", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"Java 17 安装程序",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 400, 200, NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        MessageBoxW(NULL, L"窗口创建失败!", L"错误", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
            InitCommonControlsEx(&icc);

            CreateWindowW(L"STATIC", 
                L"需要安装 Java 17 或更高版本才能运行 Minecraft 服务器",
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                20, 20, 340, 50, hWnd, (HMENU)ID_MESSAGE_STATIC, NULL, NULL);

            CreateWindowW(L"BUTTON", L"下载 Java 17", 
                WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_CENTER,
                130, 80, 120, 30, hWnd, (HMENU)ID_DOWNLOAD_BTN, NULL, NULL);
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_DOWNLOAD_BTN) {
                // 打开Java下载页面
                ShellExecuteW(NULL, L"open", L"https://www.oracle.com/java/technologies/downloads/#jdk17-windows", 
                    NULL, NULL, SW_SHOWNORMAL);
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}