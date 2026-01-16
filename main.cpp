/*
 * v3 Test Client - 简化版测试客户端
 * 
 * 功能：
 * - 启动/停止 v3 内核
 * - IPC 通信测试
 * - 连接状态显示
 * - 基本统计信息
 * 
 * 编译：cl /O2 /MT main.cpp /link user32.lib gdi32.lib shell32.lib advapi32.lib
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// =========================================================
// 常量定义（来自 v3 内核）
// =========================================================
#define V3_IPC_PIPE_NAME_FMT    L"\\\\.\\pipe\\v3_core_ipc_%u"
#define V3_IPC_MAGIC            0x50493356
#define V3_IPC_VERSION          0x0001
#define V3_IPC_TIMEOUT_MS       5000
#define V3_IPC_BUFFER_SIZE      65536

// IPC 消息类型
enum V3_IPC_MSG {
    V3_IPC_CMD_PING         = 0x0100,
    V3_IPC_CMD_PONG         = 0x0101,
    V3_IPC_CMD_CONNECT      = 0x0110,
    V3_IPC_CMD_DISCONNECT   = 0x0111,
    V3_IPC_CMD_SHUTDOWN     = 0x01FF,
    V3_IPC_CMD_GET_STATE    = 0x0300,
    V3_IPC_CMD_GET_STATS    = 0x0301,
    V3_IPC_CMD_GET_VERSION  = 0x0302,
    V3_IPC_RSP_OK           = 0x8000,
    V3_IPC_RSP_ERROR        = 0x8001,
    V3_IPC_RSP_STATE        = 0x8003,
    V3_IPC_RSP_STATS        = 0x8004,
    V3_IPC_RSP_VERSION      = 0x8005,
};

// 连接状态
enum V3_CONN_STATE {
    V3_STATE_DISCONNECTED = 0,
    V3_STATE_CONNECTING,
    V3_STATE_CONNECTED,
    V3_STATE_RECONNECTING,
    V3_STATE_DISCONNECTING,
    V3_STATE_ERROR,
};

// IPC 消息头
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t seq;
    uint32_t payload_len;
} IpcHeader;

typedef struct {
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t packets_dropped;
    uint64_t decrypt_failures;
    uint64_t magic_failures;
    uint64_t fec_groups_sent;
    uint64_t fec_groups_recv;
    uint64_t fec_recoveries;
    uint64_t fec_failures;
    uint64_t rtt_us;
    uint64_t rtt_min_us;
    uint64_t rtt_max_us;
    uint64_t jitter_us;
    uint64_t connect_time_sec;
    uint32_t reconnect_count;
    uint64_t last_send_time;
    uint64_t last_recv_time;
} V3Stats;
#pragma pack(pop)

// =========================================================
// 全局变量
// =========================================================
static HWND g_hWnd = NULL;
static HWND g_hStatus = NULL;
static HWND g_hLog = NULL;
static HWND g_hBtnStart = NULL;
static HWND g_hBtnStop = NULL;
static HWND g_hBtnConnect = NULL;
static HWND g_hBtnDisconnect = NULL;
static HWND g_hBtnPing = NULL;
static HWND g_hBtnStats = NULL;

static HANDLE g_hCoreProcess = NULL;
static DWORD g_dwCorePid = 0;
static HANDLE g_hIpcPipe = INVALID_HANDLE_VALUE;
static UINT_PTR g_uTimerId = 0;

static HFONT g_hFont = NULL;
static HFONT g_hFontMono = NULL;

// 控件 ID
#define ID_BTN_START        1001
#define ID_BTN_STOP         1002
#define ID_BTN_CONNECT      1003
#define ID_BTN_DISCONNECT   1004
#define ID_BTN_PING         1005
#define ID_BTN_STATS        1006
#define ID_BTN_CLEAR        1007
#define ID_TIMER_UPDATE     1

// =========================================================
// 日志函数
// =========================================================
static void Log(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, _countof(buf), fmt, args);
    va_end(args);
    
    // 添加时间戳
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    wchar_t line[1200];
    swprintf_s(line, _countof(line), L"[%02d:%02d:%02d] %s\r\n",
               st.wHour, st.wMinute, st.wSecond, buf);
    
    // 追加到日志控件
    int len = GetWindowTextLength(g_hLog);
    SendMessage(g_hLog, EM_SETSEL, len, len);
    SendMessage(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)line);
    SendMessage(g_hLog, EM_SCROLLCARET, 0, 0);
}

static void SetStatus(const wchar_t* text) {
    SetWindowText(g_hStatus, text);
}

// =========================================================
// IPC 通信
// =========================================================
static BOOL IpcConnect() {
    if (g_hIpcPipe != INVALID_HANDLE_VALUE) {
        return TRUE;
    }
    
    if (g_dwCorePid == 0) {
        Log(L"内核未运行");
        return FALSE;
    }
    
    wchar_t pipeName[256];
    swprintf_s(pipeName, _countof(pipeName), V3_IPC_PIPE_NAME_FMT, g_dwCorePid);
    
    // 等待管道可用
    if (!WaitNamedPipe(pipeName, V3_IPC_TIMEOUT_MS)) {
        Log(L"等待管道超时: %s", pipeName);
        return FALSE;
    }
    
    g_hIpcPipe = CreateFile(
        pipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (g_hIpcPipe == INVALID_HANDLE_VALUE) {
        Log(L"连接管道失败: %lu", GetLastError());
        return FALSE;
    }
    
    // 设置消息模式
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(g_hIpcPipe, &mode, NULL, NULL);
    
    Log(L"IPC 连接成功");
    return TRUE;
}

static void IpcDisconnect() {
    if (g_hIpcPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hIpcPipe);
        g_hIpcPipe = INVALID_HANDLE_VALUE;
        Log(L"IPC 已断开");
    }
}

static BOOL IpcSendRecv(uint16_t type, void* payload, uint32_t payloadLen,
                        uint16_t* rspType, void* rspBuf, uint32_t rspBufLen) {
    if (!IpcConnect()) {
        return FALSE;
    }
    
    // 构建请求
    uint8_t sendBuf[V3_IPC_BUFFER_SIZE];
    IpcHeader* hdr = (IpcHeader*)sendBuf;
    
    hdr->magic = V3_IPC_MAGIC;
    hdr->version = V3_IPC_VERSION;
    hdr->type = type;
    hdr->seq = GetTickCount();
    hdr->payload_len = payloadLen;
    
    if (payloadLen > 0 && payload) {
        memcpy(sendBuf + sizeof(IpcHeader), payload, payloadLen);
    }
    
    DWORD written;
    if (!WriteFile(g_hIpcPipe, sendBuf, sizeof(IpcHeader) + payloadLen, &written, NULL)) {
        Log(L"发送失败: %lu", GetLastError());
        IpcDisconnect();
        return FALSE;
    }
    
    // 读取响应
    uint8_t recvBuf[V3_IPC_BUFFER_SIZE];
    DWORD read;
    
    if (!ReadFile(g_hIpcPipe, recvBuf, sizeof(recvBuf), &read, NULL)) {
        Log(L"接收失败: %lu", GetLastError());
        IpcDisconnect();
        return FALSE;
    }
    
    if (read < sizeof(IpcHeader)) {
        Log(L"响应太短");
        return FALSE;
    }
    
    IpcHeader* rspHdr = (IpcHeader*)recvBuf;
    
    if (rspHdr->magic != V3_IPC_MAGIC) {
        Log(L"响应 Magic 错误");
        return FALSE;
    }
    
    if (rspType) *rspType = rspHdr->type;
    
    if (rspBuf && rspBufLen > 0 && rspHdr->payload_len > 0) {
        uint32_t copyLen = min(rspBufLen, rspHdr->payload_len);
        memcpy(rspBuf, recvBuf + sizeof(IpcHeader), copyLen);
    }
    
    return TRUE;
}

// =========================================================
// 内核进程管理
// =========================================================
static BOOL StartCore() {
    if (g_hCoreProcess != NULL) {
        Log(L"内核已在运行");
        return TRUE;
    }
    
    // 获取当前目录
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    
    wchar_t corePath[MAX_PATH];
    swprintf_s(corePath, _countof(corePath), L"%s\\core\\v3_core.exe", exePath);
    
    // 检查文件是否存在
    if (GetFileAttributes(corePath) == INVALID_FILE_ATTRIBUTES) {
        // 尝试当前目录
        swprintf_s(corePath, _countof(corePath), L"%s\\v3_core.exe", exePath);
        if (GetFileAttributes(corePath) == INVALID_FILE_ATTRIBUTES) {
            Log(L"找不到 v3_core.exe");
            return FALSE;
        }
    }
    
    Log(L"启动内核: %s", corePath);
    
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    
    // 创建进程
    wchar_t cmdLine[MAX_PATH + 64];
    swprintf_s(cmdLine, _countof(cmdLine), L"\"%s\" -v", corePath);
    
    if (!CreateProcess(
            NULL,
            cmdLine,
            NULL, NULL,
            FALSE,
            CREATE_NEW_CONSOLE,  // 使用新控制台以便查看输出
            NULL,
            exePath,
            &si, &pi)) {
        Log(L"启动内核失败: %lu", GetLastError());
        return FALSE;
    }
    
    g_hCoreProcess = pi.hProcess;
    g_dwCorePid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    
    Log(L"内核已启动, PID=%lu", g_dwCorePid);
    SetStatus(L"内核运行中");
    
    // 等待内核初始化
    Sleep(1000);
    
    return TRUE;
}

static void StopCore() {
    if (g_hCoreProcess == NULL) {
        Log(L"内核未运行");
        return;
    }
    
    // 先尝试通过 IPC 优雅关闭
    if (g_hIpcPipe != INVALID_HANDLE_VALUE) {
        Log(L"发送关闭命令...");
        uint16_t rspType;
        IpcSendRecv(V3_IPC_CMD_SHUTDOWN, NULL, 0, &rspType, NULL, 0);
        IpcDisconnect();
        
        // 等待进程退出
        if (WaitForSingleObject(g_hCoreProcess, 3000) == WAIT_OBJECT_0) {
            Log(L"内核已优雅关闭");
            goto cleanup;
        }
    }
    
    // 强制终止
    Log(L"强制终止内核...");
    TerminateProcess(g_hCoreProcess, 1);
    WaitForSingleObject(g_hCoreProcess, 1000);
    
cleanup:
    CloseHandle(g_hCoreProcess);
    g_hCoreProcess = NULL;
    g_dwCorePid = 0;
    
    Log(L"内核已停止");
    SetStatus(L"内核已停止");
}

// =========================================================
// 命令处理
// =========================================================
static void DoPing() {
    Log(L"发送 PING...");
    
    uint16_t rspType;
    if (IpcSendRecv(V3_IPC_CMD_PING, NULL, 0, &rspType, NULL, 0)) {
        if (rspType == V3_IPC_CMD_PONG) {
            Log(L"收到 PONG - 内核正常响应!");
        } else {
            Log(L"收到异常响应: 0x%04X", rspType);
        }
    } else {
        Log(L"PING 失败");
    }
}

static void DoConnect() {
    Log(L"请求连接...");
    
    uint16_t rspType;
    if (IpcSendRecv(V3_IPC_CMD_CONNECT, NULL, 0, &rspType, NULL, 0)) {
        if (rspType == V3_IPC_RSP_OK) {
            Log(L"连接命令已发送");
        } else {
            Log(L"连接失败, 响应: 0x%04X", rspType);
        }
    }
}

static void DoDisconnect() {
    Log(L"请求断开...");
    
    uint16_t rspType;
    if (IpcSendRecv(V3_IPC_CMD_DISCONNECT, NULL, 0, &rspType, NULL, 0)) {
        if (rspType == V3_IPC_RSP_OK) {
            Log(L"断开命令已发送");
        } else {
            Log(L"断开失败, 响应: 0x%04X", rspType);
        }
    }
}

static void DoGetStats() {
    Log(L"获取统计信息...");
    
    uint16_t rspType;
    V3Stats stats = { 0 };
    
    if (IpcSendRecv(V3_IPC_CMD_GET_STATS, NULL, 0, &rspType, &stats, sizeof(stats))) {
        if (rspType == V3_IPC_RSP_STATS) {
            Log(L"========== 统计信息 ==========");
            Log(L"发送包数: %llu", stats.packets_sent);
            Log(L"接收包数: %llu", stats.packets_recv);
            Log(L"发送字节: %llu", stats.bytes_sent);
            Log(L"接收字节: %llu", stats.bytes_recv);
            Log(L"丢弃包数: %llu", stats.packets_dropped);
            Log(L"解密失败: %llu", stats.decrypt_failures);
            Log(L"Magic错误: %llu", stats.magic_failures);
            Log(L"RTT: %llu us", stats.rtt_us);
            Log(L"FEC恢复: %llu", stats.fec_recoveries);
            Log(L"重连次数: %lu", stats.reconnect_count);
            Log(L"================================");
        } else {
            Log(L"获取统计失败, 响应: 0x%04X", rspType);
        }
    }
}

static void DoGetState() {
    uint16_t rspType;
    uint32_t state = 0;
    
    if (IpcSendRecv(V3_IPC_CMD_GET_STATE, NULL, 0, &rspType, &state, sizeof(state))) {
        if (rspType == V3_IPC_RSP_STATE) {
            const wchar_t* stateStr = L"未知";
            switch (state) {
                case V3_STATE_DISCONNECTED: stateStr = L"未连接"; break;
                case V3_STATE_CONNECTING:   stateStr = L"连接中"; break;
                case V3_STATE_CONNECTED:    stateStr = L"已连接"; break;
                case V3_STATE_RECONNECTING: stateStr = L"重连中"; break;
                case V3_STATE_DISCONNECTING:stateStr = L"断开中"; break;
                case V3_STATE_ERROR:        stateStr = L"错误"; break;
            }
            SetStatus(stateStr);
        }
    }
}

// =========================================================
// 窗口过程
// =========================================================
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        {
            // 创建字体
            g_hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            
            g_hFontMono = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     FIXED_PITCH | FF_MODERN, L"Consolas");
            
            int y = 10;
            int btnW = 90, btnH = 30;
            int x = 10;
            
            // 状态标签
            g_hStatus = CreateWindow(L"STATIC", L"内核未启动",
                                     WS_CHILD | WS_VISIBLE | SS_CENTER,
                                     x, y, 380, 25, hWnd, NULL, NULL, NULL);
            SendMessage(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            y += 35;
            
            // 按钮行1: 内核控制
            g_hBtnStart = CreateWindow(L"BUTTON", L"启动内核",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       x, y, btnW, btnH, hWnd, (HMENU)ID_BTN_START, NULL, NULL);
            SendMessage(g_hBtnStart, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            x += btnW + 10;
            
            g_hBtnStop = CreateWindow(L"BUTTON", L"停止内核",
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      x, y, btnW, btnH, hWnd, (HMENU)ID_BTN_STOP, NULL, NULL);
            SendMessage(g_hBtnStop, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            x += btnW + 10;
            
            g_hBtnPing = CreateWindow(L"BUTTON", L"PING",
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      x, y, btnW, btnH, hWnd, (HMENU)ID_BTN_PING, NULL, NULL);
            SendMessage(g_hBtnPing, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            x += btnW + 10;
            
            g_hBtnStats = CreateWindow(L"BUTTON", L"统计",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       x, y, btnW, btnH, hWnd, (HMENU)ID_BTN_STATS, NULL, NULL);
            SendMessage(g_hBtnStats, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            
            y += btnH + 10;
            x = 10;
            
            // 按钮行2: 连接控制
            g_hBtnConnect = CreateWindow(L"BUTTON", L"连接",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         x, y, btnW, btnH, hWnd, (HMENU)ID_BTN_CONNECT, NULL, NULL);
            SendMessage(g_hBtnConnect, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            x += btnW + 10;
            
            g_hBtnDisconnect = CreateWindow(L"BUTTON", L"断开",
                                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                            x, y, btnW, btnH, hWnd, (HMENU)ID_BTN_DISCONNECT, NULL, NULL);
            SendMessage(g_hBtnDisconnect, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            x += btnW + 10;
            
            CreateWindow(L"BUTTON", L"清空日志",
                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         x, y, btnW, btnH, hWnd, (HMENU)ID_BTN_CLEAR, NULL, NULL);
            
            y += btnH + 10;
            
            // 日志区域
            g_hLog = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                    10, y, 380, 300, hWnd, NULL, NULL, NULL);
            SendMessage(g_hLog, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
            
            Log(L"v3 测试客户端已启动");
            Log(L"点击 [启动内核] 开始测试");
            
            // 启动定时器
            g_uTimerId = SetTimer(hWnd, ID_TIMER_UPDATE, 2000, NULL);
        }
        return 0;
        
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_START:
            StartCore();
            break;
        case ID_BTN_STOP:
            StopCore();
            break;
        case ID_BTN_CONNECT:
            DoConnect();
            break;
        case ID_BTN_DISCONNECT:
            DoDisconnect();
            break;
        case ID_BTN_PING:
            DoPing();
            break;
        case ID_BTN_STATS:
            DoGetStats();
            break;
        case ID_BTN_CLEAR:
            SetWindowText(g_hLog, L"");
            break;
        }
        return 0;
        
    case WM_TIMER:
        if (wParam == ID_TIMER_UPDATE) {
            // 定期检查内核状态
            if (g_hCoreProcess != NULL) {
                DWORD exitCode;
                if (GetExitCodeProcess(g_hCoreProcess, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        Log(L"内核已退出, 退出码: %lu", exitCode);
                        CloseHandle(g_hCoreProcess);
                        g_hCoreProcess = NULL;
                        g_dwCorePid = 0;
                        IpcDisconnect();
                        SetStatus(L"内核已退出");
                    } else {
                        // 获取状态
                        DoGetState();
                    }
                }
            }
        }
        return 0;
        
    case WM_CLOSE:
        if (g_hCoreProcess != NULL) {
            if (MessageBox(hWnd, L"内核正在运行，是否一并关闭？",
                          L"确认退出", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                StopCore();
            }
        }
        DestroyWindow(hWnd);
        return 0;
        
    case WM_DESTROY:
        if (g_uTimerId) {
            KillTimer(hWnd, g_uTimerId);
        }
        IpcDisconnect();
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontMono) DeleteObject(g_hFontMono);
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// =========================================================
// 程序入口
// =========================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // 注册窗口类
    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"V3TestClient";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"窗口类注册失败", L"错误", MB_ICONERROR);
        return 1;
    }
    
    // 创建窗口
    g_hWnd = CreateWindowEx(
        0,
        L"V3TestClient",
        L"v3 测试客户端",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        420, 460,
        NULL, NULL, hInstance, NULL
    );
    
    if (!g_hWnd) {
        MessageBox(NULL, L"窗口创建失败", L"错误", MB_ICONERROR);
        return 1;
    }
    
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}

// 控制台入口（用于调试）
int wmain(int argc, wchar_t* argv[]) {
    return wWinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOW);
}
