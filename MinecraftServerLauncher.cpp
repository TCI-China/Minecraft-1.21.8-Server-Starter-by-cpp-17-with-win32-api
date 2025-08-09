#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <io.h>
#include <fcntl.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")

#define ID_START_BTN 101
#define ID_LOG_EDIT 102
#define ID_PLAYER_LIST 103
#define ID_STATUS_BAR 104
#define ID_TERMINATE_BTN 105
#define WM_UPDATE_LOG (WM_USER + 1)
#define WM_UPDATE_PLAYERS (WM_USER + 2)

HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
HANDLE g_hServerProcess = NULL;
HWND g_hLogEdit, g_hPlayerList, g_hStatusBar;
std::vector<std::wstring> g_players;

DWORD WINAPI ReadOutputThread(LPVOID lpParam);
void CheckJavaVersion(HWND hWnd);
bool CheckEulaAgreement();
void StartServerProcess(HWND hWnd);
void ParseLogForPlayers(const std::wstring& log);
void UpdatePlayerList();
void CleanupProcess();

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
            InitCommonControlsEx(&icc);

            // 创建界面元素
            CreateWindowW(L"BUTTON", L"启动服务器", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                10, 10, 120, 30, hWnd, (HMENU)ID_START_BTN, NULL, NULL);

            CreateWindowW(L"BUTTON", L"终止服务器", WS_VISIBLE | WS_CHILD,
                140, 10, 120, 30, hWnd, (HMENU)ID_TERMINATE_BTN, NULL, NULL);

            g_hLogEdit = CreateWindowW(L"EDIT", NULL, 
                WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                10, 50, 760, 300, hWnd, (HMENU)ID_LOG_EDIT, NULL, NULL);

            CreateWindowW(L"STATIC", L"在线玩家:", WS_VISIBLE | WS_CHILD,
                10, 360, 100, 20, hWnd, NULL, NULL, NULL);

            g_hPlayerList = CreateWindowW(WC_LISTVIEWW, L"", 
                WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER,
                10, 380, 200, 150, hWnd, (HMENU)ID_PLAYER_LIST, NULL, NULL);

            LVCOLUMN lvc = { 0 };
            lvc.mask = LVCF_WIDTH;
            lvc.cx = 180;
            ListView_InsertColumn(g_hPlayerList, 0, &lvc);

            g_hStatusBar = CreateWindowW(STATUSCLASSNAMEW, NULL,
                WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP,
                0, 0, 0, 0, hWnd, (HMENU)ID_STATUS_BAR, NULL, NULL);

            // 检查Java环境
            CheckJavaVersion(hWnd);
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_START_BTN) {
                if (!CheckEulaAgreement()) {
                    MessageBoxW(hWnd, L"请先同意EULA协议 (eula.txt中设置eula=true)", L"EULA协议未接受", MB_ICONWARNING);
                    return 0;
                }
                StartServerProcess(hWnd);
            } 
            else if (LOWORD(wParam) == ID_TERMINATE_BTN) {
                if (g_hServerProcess) {
                    TerminateProcess(g_hServerProcess, 0);
                    CleanupProcess();
                    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"服务器已终止");
                }
            }
            break;
        }

        case WM_SIZE: {
            RECT rc;
            GetClientRect(hWnd, &rc);
            
            // 调整状态栏
            SendMessageW(g_hStatusBar, WM_SIZE, 0, 0);
            
            // 调整日志框
            MoveWindow(g_hLogEdit, 10, 50, rc.right - 20, rc.bottom - 170, TRUE);
            
            // 调整玩家列表
            MoveWindow(g_hPlayerList, 10, rc.bottom - 150, 200, 140, TRUE);
            
            // 调整按钮位置
            HWND hBtn = GetDlgItem(hWnd, ID_START_BTN);
            MoveWindow(hBtn, 10, 10, 120, 30, TRUE);
            
            hBtn = GetDlgItem(hWnd, ID_TERMINATE_BTN);
            MoveWindow(hBtn, 140, 10, 120, 30, TRUE);
            break;
        }

        case WM_UPDATE_LOG: {
            wchar_t* logText = (wchar_t*)lParam;
            if (logText) {
                int len = GetWindowTextLengthW(g_hLogEdit);
                SendMessageW(g_hLogEdit, EM_SETSEL, len, len);
                SendMessageW(g_hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)logText);
                delete[] logText;
            }
            break;
        }

        case WM_UPDATE_PLAYERS: {
            UpdatePlayerList();
            break;
        }

        case WM_CLOSE:
            if (g_hServerProcess) {
                if (MessageBoxW(hWnd, L"服务器仍在运行，确定要退出吗？", L"确认退出", MB_YESNO | MB_ICONQUESTION) == IDNO) {
                    return 0;
                }
                TerminateProcess(g_hServerProcess, 0);
                CleanupProcess();
            }
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            CleanupProcess();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MinecraftServerLauncher";
    
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"窗口注册失败!", L"错误", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"Minecraft 1.21.8 服务器启动器",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

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

void CheckJavaVersion(HWND hWnd) {
    HKEY hKey;
    DWORD dwType = REG_SZ;
    wchar_t version[16] = {0};
    DWORD cbData = sizeof(version);

    // 1. 检查注册表中Java Runtime Environment版本
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\JavaSoft\\Java Runtime Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"CurrentVersion", NULL, &dwType, (LPBYTE)version, &cbData) == ERROR_SUCCESS) {
            // 检查版本号是否>=17
            int majorVer = _wtoi(version);
            if (majorVer >= 17) {
                SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"已检测到Java 17或更高版本");
                RegCloseKey(hKey);
                return;
            }
        }
        RegCloseKey(hKey);
    }

    // 2. 检查注册表中Java Development Kit版本
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\JavaSoft\\Java Development Kit", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"CurrentVersion", NULL, &dwType, (LPBYTE)version, &cbData) == ERROR_SUCCESS) {
            // 检查版本号是否>=17
            int majorVer = _wtoi(version);
            if (majorVer >= 17) {
                SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"已检测到Java 17或更高版本");
                RegCloseKey(hKey);
                return;
            }
        }
        RegCloseKey(hKey);
    }

    // 3. 尝试执行java -version
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &sa, 0)) {
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"Java检测失败");
        return;
    }

    PROCESS_INFORMATION pi;
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = g_hChildStd_OUT_Wr;
    si.hStdError = g_hChildStd_OUT_Wr;

    wchar_t cmd[] = L"java -version";
    if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, 2000);
        
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(g_hChildStd_OUT_Wr);
        
        if (exitCode == 0) {
            char buffer[512] = {0};
            DWORD bytesRead;
            ReadFile(g_hChildStd_OUT_Rd, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
            buffer[bytesRead] = '\0';
            
            // 解析版本
            std::string output(buffer);
            size_t verPos = output.find("version \"");
            if (verPos != std::string::npos) {
                std::string verStr = output.substr(verPos + 9);
                int majorVer = std::stoi(verStr);
                if (majorVer >= 17) {
                    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"已检测到Java 17或更高版本");
                    CloseHandle(g_hChildStd_OUT_Rd);
                    return;
                }
            }
        }
        CloseHandle(g_hChildStd_OUT_Rd);
    }

    // 未找到Java 17+
    SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"未找到Java 17或更高版本");
    
    // 启动应用程序b
    STARTUPINFOW si2 = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION pi2;
    wchar_t appName[] = L"JavaInstaller.exe";
    if (CreateProcessW(appName, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si2, &pi2)) {
        CloseHandle(pi2.hThread);
        CloseHandle(pi2.hProcess);
    } else {
        MessageBoxW(hWnd, L"无法启动Java安装程序", L"错误", MB_ICONERROR);
    }
    
    // 退出应用程序
    PostQuitMessage(0);
}

bool CheckEulaAgreement() {
    std::ifstream eulaFile("eula.txt");
    if (!eulaFile.is_open()) {
        // 尝试创建默认的eula.txt文件
        std::ofstream newEula("eula.txt");
        if (newEula) {
            newEula << "eula=false\n";
            newEula.close();
        }
        return false;
    }

    std::string line;
    while (std::getline(eulaFile, line)) {
        if (line.find("eula=") == 0) {
            eulaFile.close();
            return line.find("true") != std::string::npos;
        }
    }
    eulaFile.close();
    return false;
}

void StartServerProcess(HWND hWnd) {
    if (g_hServerProcess) {
        MessageBoxW(hWnd, L"服务器已在运行中", L"警告", MB_ICONWARNING);
        return;
    }

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &sa, 0)) {
        MessageBoxW(hWnd, L"无法创建输出管道", L"错误", MB_ICONERROR);
        return;
    }

    PROCESS_INFORMATION pi;
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = g_hChildStd_OUT_Wr;
    si.hStdError = g_hChildStd_OUT_Wr;

    wchar_t cmdLine[] = L"java -Xmx1024M -Xms1024M -jar server.jar nogui";
    if (CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        g_hServerProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        CloseHandle(g_hChildStd_OUT_Wr);
        SendMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"服务器运行中...");

        // 创建读取输出的线程
        CreateThread(NULL, 0, ReadOutputThread, hWnd, 0, NULL);
    } else {
        DWORD err = GetLastError();
        wchar_t errMsg[256];
        wsprintfW(errMsg, L"无法启动服务器 (错误码: %d)", err);
        MessageBoxW(hWnd, errMsg, L"错误", MB_ICONERROR);
        CleanupProcess();
    }
}

DWORD WINAPI ReadOutputThread(LPVOID lpParam) {
    HWND hWnd = (HWND)lpParam;
    DWORD bytesRead;
    char buffer[4096];
    std::wstring accumulatedLog;

    while (ReadFile(g_hChildStd_OUT_Rd, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        
        // 转换ANSI到Unicode
        wchar_t wBuffer[4096];
        int wLen = MultiByteToWideChar(CP_ACP, 0, buffer, bytesRead, wBuffer, 4096);
        if (wLen > 0) {
            wBuffer[wLen] = L'\0';
            accumulatedLog += wBuffer;
            
            // 检查换行符并发送更新
            size_t pos;
            while ((pos = accumulatedLog.find(L'\n')) != std::wstring::npos) {
                std::wstring line = accumulatedLog.substr(0, pos);
                accumulatedLog.erase(0, pos + 1);
                
                // 检查玩家加入/离开
                if (line.find(L"joined the game") != std::wstring::npos ||
                    line.find(L"left the game") != std::wstring::npos) {
                    ParseLogForPlayers(line);
                    PostMessageW(hWnd, WM_UPDATE_PLAYERS, 0, 0);
                }
                
                // 发送日志更新
                wchar_t* logLine = new wchar_t[line.length() + 3];  // 额外空间用于换行符
                wcscpy(logLine, line.c_str());
                wcscat(logLine, L"\r\n");
                PostMessageW(hWnd, WM_UPDATE_LOG, 0, (LPARAM)logLine);
            }
        }
    }

    // 处理剩余日志
    if (!accumulatedLog.empty()) {
        wchar_t* logLine = new wchar_t[accumulatedLog.length() + 3];
        wcscpy(logLine, accumulatedLog.c_str());
        wcscat(logLine, L"\r\n");
        PostMessageW(hWnd, WM_UPDATE_LOG, 0, (LPARAM)logLine);
    }

    // 服务器进程结束
    DWORD exitCode;
    GetExitCodeProcess(g_hServerProcess, &exitCode);
    CleanupProcess();
    
    wchar_t statusMsg[128];
    wsprintfW(statusMsg, L"服务器已退出 (代码: %d)", exitCode);
    PostMessageW(g_hStatusBar, SB_SETTEXTW, 0, (LPARAM)statusMsg);
    
    return 0;
}

void ParseLogForPlayers(const std::wstring& log) {
    static const std::wstring JOIN_STR = L"joined the game";
    static const std::wstring LEAVE_STR = L"left the game";
    
    size_t eventPos = log.find(JOIN_STR);
    bool isJoin = true;
    
    if (eventPos == std::wstring::npos) {
        eventPos = log.find(LEAVE_STR);
        isJoin = false;
        if (eventPos == std::wstring::npos) return;
    }
    
    // 查找玩家名称位置 [时间] [服务端线程/信息]: 玩家名称 joined/left...
    size_t colonPos = log.find(L':', log.find(L']') + 1); // 找到第二个冒号
    if (colonPos == std::wstring::npos || colonPos > eventPos) return;
    
    // 玩家名称在冒号后两个字符位置
    std::wstring player = log.substr(colonPos + 2, eventPos - colonPos - 3);
    
    if (isJoin) {
        if (std::find(g_players.begin(), g_players.end(), player) == g_players.end()) {
            g_players.push_back(player);
        }
    } else {
        auto it = std::find(g_players.begin(), g_players.end(), player);
        if (it != g_players.end()) {
            g_players.erase(it);
        }
    }
}

void UpdatePlayerList() {
    ListView_DeleteAllItems(g_hPlayerList);
    
    for (size_t i = 0; i < g_players.size(); i++) {
        LVITEMW lvi = { 0 };
        lvi.mask = LVIF_TEXT;
        lvi.iItem = static_cast<int>(i);
        lvi.pszText = const_cast<wchar_t*>(g_players[i].c_str());
        ListView_InsertItem(g_hPlayerList, &lvi);
    }
}

void CleanupProcess() {
    if (g_hServerProcess) {
        CloseHandle(g_hServerProcess);
        g_hServerProcess = NULL;
    }
    if (g_hChildStd_OUT_Rd) {
        CloseHandle(g_hChildStd_OUT_Rd);
        g_hChildStd_OUT_Rd = NULL;
    }
    if (g_hChildStd_OUT_Wr) {
        CloseHandle(g_hChildStd_OUT_Wr);
        g_hChildStd_OUT_Wr = NULL;
    }
    g_players.clear();
}