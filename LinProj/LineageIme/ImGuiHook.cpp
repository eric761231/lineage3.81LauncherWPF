// ImGuiHook.cpp: 對 IDirect3DDevice9 進行 EndScene/Reset vtable hook，
// 讓 Dear ImGui 疊層真正畫在遊戲畫面上。做法跟 LauncherDll\ImGuiHook.cpp
// 相同（各自獨立注入、各自靜態連結一份 imgui、各自 hook 同一個
// EndScene/Reset 位址——Detours 會自動串接成 hook chain，不會互相覆蓋）：
// 建立一個「假的」隱藏視窗 + IDirect3D9 + CreateDevice 拿到一個暫時
// device，讀出它的 EndScene/Reset vtable slot 位址後就釋放掉這個暫時
// device——同一份 d3d9.dll 產生的所有 device 共用同一份 vtable，所以 hook
// 到的位址對遊戲真正在用的 device 一樣有效。
#include "Common.h"
#include "ImGuiHook.h"
#include <d3d9.h>
#include "detours.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "Dbg.h"

#pragma comment(lib, "d3d9.lib")

// imgui_impl_win32.h 故意把這個宣告放在 #if 0 區塊，避免這個 header 依賴
// <windows.h>；呼叫端要自己在能取得 <windows.h> 的地方複製一份宣告。
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                              WPARAM wParam,
                                                              LPARAM lParam);

namespace {

typedef HRESULT(WINAPI *EndScene_t)(IDirect3DDevice9 *);
typedef HRESULT(WINAPI *Reset_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);

EndScene_t g_RealEndScene = nullptr;
Reset_t g_RealReset = nullptr;

HWND g_GameHwnd = nullptr;
bool g_ImGuiInited = false;
bool g_ShowOverlay = true;
bool g_InsertWasDown = false;

void ToggleOverlayIfHotkeyPressed() {
  bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
  if (down && !g_InsertWasDown) {
    g_ShowOverlay = !g_ShowOverlay;
  }
  g_InsertWasDown = down;
}

HRESULT WINAPI Hooked_EndScene(IDirect3DDevice9 *device) {
  if (!g_ImGuiInited) {
    if (!g_GameHwnd || !IsWindow(g_GameHwnd)) {
      return g_RealEndScene(device);
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // 不落地寫 imgui.ini
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_GameHwnd);
    ImGui_ImplDX9_Init(device);
    g_ImGuiInited = true;
    IME_LOG("[ImGuiHook] ImGui initialized, hwnd=%p device=%p", g_GameHwnd, device);
  }

  ToggleOverlayIfHotkeyPressed();

  ImGui_ImplDX9_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  if (g_ShowOverlay) {
    ImGui::Begin("LineageIme ImGui Test");
    ImGui::Text("imgui %s integrated OK", IMGUI_VERSION);
    ImGui::Text("Press INSERT to toggle this overlay");
    static int clicks = 0;
    if (ImGui::Button("Click me")) clicks++;
    ImGui::Text("clicks = %d", clicks);
    ImGui::End();
    ImGui::ShowDemoWindow();
  }

  ImGui::EndFrame();
  ImGui::Render();
  ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

  return g_RealEndScene(device);
}

HRESULT WINAPI Hooked_Reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *pp) {
  if (g_ImGuiInited) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
  }
  HRESULT hr = g_RealReset(device, pp);
  if (g_ImGuiInited) {
    ImGui_ImplDX9_CreateDeviceObjects();
  }
  return hr;
}

// 從一個暫時、隱藏的 dummy device 讀出 EndScene/Reset 的 vtable 位址。
bool CaptureD3D9VTable(void **outEndScene, void **outReset) {
  WNDCLASSEXA wc = {sizeof(WNDCLASSEXA)};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "LineageImeImGuiDummyWndClass";
  RegisterClassExA(&wc);
  HWND dummyHwnd = CreateWindowExA(0, wc.lpszClassName, "dummy", WS_OVERLAPPEDWINDOW,
                                    0, 0, 100, 100, nullptr, nullptr, wc.hInstance,
                                    nullptr);
  if (!dummyHwnd) return false;

  IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
  if (!d3d) {
    DestroyWindow(dummyHwnd);
    return false;
  }

  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.hDeviceWindow = dummyHwnd;

  IDirect3DDevice9 *device = nullptr;
  HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummyHwnd,
                                  D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
  if (FAILED(hr) || !device) {
    d3d->Release();
    DestroyWindow(dummyHwnd);
    return false;
  }

  void **vtable = *reinterpret_cast<void ***>(device);
  *outEndScene = vtable[42]; // IDirect3DDevice9::EndScene
  *outReset = vtable[16];    // IDirect3DDevice9::Reset

  device->Release();
  d3d->Release();
  DestroyWindow(dummyHwnd);
  return true;
}

} // namespace

void InstallImGuiHook(HWND gameHwnd) {
  g_GameHwnd = gameHwnd;

  void *endSceneAddr = nullptr;
  void *resetAddr = nullptr;
  if (!CaptureD3D9VTable(&endSceneAddr, &resetAddr)) {
    IME_LOG("[ImGuiHook] CaptureD3D9VTable failed, imgui overlay disabled");
    return;
  }

  g_RealEndScene = reinterpret_cast<EndScene_t>(endSceneAddr);
  g_RealReset = reinterpret_cast<Reset_t>(resetAddr);

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)g_RealEndScene, reinterpret_cast<PVOID>(Hooked_EndScene));
  DetourAttach(&(PVOID &)g_RealReset, reinterpret_cast<PVOID>(Hooked_Reset));
  LONG result = DetourTransactionCommit();
  IME_LOG("[ImGuiHook] D3D9 EndScene/Reset hook install result=%ld", result);
}

bool ImGuiHook_HandleWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              LRESULT *outResult) {
  if (!g_ImGuiInited || !g_ShowOverlay) return false;

  ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

  ImGuiIO &io = ImGui::GetIO();
  bool isMouseMsg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEWHEEL;
  bool isKeyMsg = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
  if ((isMouseMsg && io.WantCaptureMouse) || (isKeyMsg && io.WantCaptureKeyboard)) {
    if (outResult) *outResult = 1;
    return true;
  }
  return false;
}
