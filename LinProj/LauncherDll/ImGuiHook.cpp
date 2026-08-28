// ImGuiHook.cpp: hooks IDirect3DDevice9::EndScene/Reset via vtable patching
// so a Dear ImGui overlay actually renders into the game's D3D9 frame.
//
// How the real device's vtable is obtained: create a temporary hidden dummy
// window + IDirect3D9 + CreateDevice to get a throwaway device, read the
// EndScene/Reset vtable slot addresses off it, then release the dummy
// device immediately. All devices created from the same d3d9.dll share the
// same vtable layout, so the addresses we hook are valid for the game's
// real device too - this is the standard approach used by overlay injection
// libraries like kiero. We never need to actually capture the game's own
// device pointer to install the hook.
#include "stdafx.h"
#include "ImGuiHook.h"
#include <d3d9.h>
#include "detours.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"

#pragma comment(lib, "d3d9.lib")

extern HWND g_hGameWnd;

// Each translation unit in this project owns its own tiny logger (see
// NetLog in DisconnectHook.cpp, the fopen_s-based logger in
// MimirPowerHook.cpp) rather than sharing one - launcherdll_net_log in
// LauncherDll.cpp is `static` (internal linkage) so it can't be linked to
// from here. OutputDebugStringA is enough for this overlay: visible via
// DebugView/an attached debugger without managing a log file path.
static void ImGuiLog(const char *fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf) - 2, fmt, args);
  va_end(args);
  strcat_s(buf, sizeof(buf), "\n");
  OutputDebugStringA(buf);
}

// imgui_impl_win32.h intentionally keeps this declaration inside a #if 0
// block to avoid pulling <windows.h> into that header; callers are expected
// to copy the forward declaration themselves once <windows.h> is available.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                              WPARAM wParam,
                                                              LPARAM lParam);

namespace {

typedef HRESULT(WINAPI *EndScene_t)(IDirect3DDevice9 *);
typedef HRESULT(WINAPI *Reset_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);

EndScene_t g_RealEndScene = nullptr;
Reset_t g_RealReset = nullptr;

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
    HWND hwnd = g_hGameWnd;
    if (!hwnd || !IsWindow(hwnd)) {
      // Game window not captured yet, skip this frame and retry next time.
      return g_RealEndScene(device);
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // don't write imgui.ini into the game dir
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(device);
    g_ImGuiInited = true;
    ImGuiLog("[ImGuiHook] ImGui initialized, hwnd=%p device=%p", hwnd,
                        device);
  }

  ToggleOverlayIfHotkeyPressed();

  ImGui_ImplDX9_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  if (g_ShowOverlay) {
    ImGui::Begin("LauncherDll ImGui Test");
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

// Reads the EndScene/Reset vtable slot addresses off a throwaway dummy
// device.
bool CaptureD3D9VTable(void **outEndScene, void **outReset) {
  WNDCLASSEXA wc = {sizeof(WNDCLASSEXA)};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "LauncherDllImGuiDummyWndClass";
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

void InstallImGuiHook() {
  void *endSceneAddr = nullptr;
  void *resetAddr = nullptr;
  if (!CaptureD3D9VTable(&endSceneAddr, &resetAddr)) {
    ImGuiLog("[ImGuiHook] CaptureD3D9VTable failed, imgui overlay disabled");
    return;
  }

  g_RealEndScene = reinterpret_cast<EndScene_t>(endSceneAddr);
  g_RealReset = reinterpret_cast<Reset_t>(resetAddr);

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID &)g_RealEndScene, reinterpret_cast<PVOID>(Hooked_EndScene));
  DetourAttach(&(PVOID &)g_RealReset, reinterpret_cast<PVOID>(Hooked_Reset));
  LONG result = DetourTransactionCommit();
  ImGuiLog("[ImGuiHook] D3D9 EndScene/Reset hook install result=%ld",
                      result);
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
