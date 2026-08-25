// TsfSink.cpp: 對照 Rust 版 ime_overlay/src/tsf_sink.rs。
//
// STA/訊息迴圈：TSF ThreadMgr 必須跑在 STA（COINIT_APARTMENTTHREADED）。
// worker thread 已經有 GetMessageW 迴圈派發 SetWinEventHook 的 OUTOFCONTEXT
// callback，順便就 pump COM 訊息 —— 一條 thread 兩用（在 dllmain.cpp 裡）。
#include "Common.h"
#include "TsfSink.h"
#include "Overlay.h"
#include "Candidates.h"
#include "Dbg.h"
#include <msctf.h>
#include <atomic>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace {

std::atomic<HWND> g_lastFocusEdit{NULL};
ITfUIElementMgr *g_uiElementMgr = nullptr;
ITfThreadMgr *g_threadMgr = nullptr;

enum class EventKind { Begin, Update, End };

void HandleEvent(DWORD id, EventKind kind) {
  if (kind == EventKind::End) {
    IME_LOG("[tsf] END id=%lu", (unsigned long)id);
    HideOverlay();
    return;
  }

  if (!g_uiElementMgr) return;
  ITfUIElement *elem = nullptr;
  if (FAILED(g_uiElementMgr->GetUIElement(id, &elem)) || !elem) {
    IME_LOG("[tsf] GetUIElement(%lu) failed", (unsigned long)id);
    return;
  }

  ITfCandidateListUIElement *cand = nullptr;
  HRESULT hr = elem->QueryInterface(IID_ITfCandidateListUIElement, (void **)&cand);
  elem->Release();
  if (FAILED(hr) || !cand) {
    // 可能是 reading info element 或別的（忽略）
    return;
  }

  UINT count = 0;
  cand->GetCount(&count);
  if (count == 0) {
    cand->Release();
    return;
  }
  UINT selection = 0;
  cand->GetSelection(&selection);
  UINT currentPage = 0;
  cand->GetCurrentPage(&currentPage);

  std::vector<UINT> pages(32, 0);
  UINT got = 0;
  cand->GetPageIndex(pages.data(), (UINT)pages.size(), &got);
  if (got > pages.size()) got = (UINT)pages.size();

  UINT pageStart = (currentPage < got) ? pages[currentPage] : 0;
  UINT nextPageStart = (currentPage + 1 < got) ? pages[currentPage + 1] : count;
  UINT pageSize = (nextPageStart > pageStart) ? (nextPageStart - pageStart) : 0;

  size_t visible = pageSize < 9 ? pageSize : 9;
  ImeState state;
  state.total = count;
  state.selection = selection;
  state.pageStart = pageStart;
  state.pageSize = (UINT)visible;
  state.items.reserve(visible);
  for (size_t i = 0; i < visible; i++) {
    UINT idx = pageStart + (UINT)i;
    BSTR bstr = nullptr;
    if (SUCCEEDED(cand->GetString(idx, &bstr)) && bstr) {
      state.items.push_back(std::wstring(bstr, SysStringLen(bstr)));
      SysFreeString(bstr);
    } else {
      state.items.push_back(L"");
    }
  }
  cand->Release();

  HWND focus = g_lastFocusEdit.load();
  const char *label = kind == EventKind::Begin ? "BEGIN" : "UPDATE";
  IME_LOG("[tsf] %s id=%lu count=%u sel=%u page=(%u,+%zu) focus=0x%p", label, (unsigned long)id,
          count, selection, (size_t)pageStart, visible, focus);
  if (!focus) return;

  // 實測發現：這台機器上的輸入法（TSF-based，例如新注音/新酷音）從頭到尾只送
  // UpdateUIElement，從沒送過 BeginUIElement——原本邏輯假設 Begin 一定先到，
  // 只有 Begin 才會呼叫 ShowOverlayFor（負責 ShowWindow + PositionNear），
  // Update 只呼叫 UpdateOverlay（只重繪內容，不顯示、也不定位，定位靠
  // g_attachedTo 這個只有 ShowOverlayFor 會設的全域變數）。結果視窗從來沒被
  // 顯示過、位置也卡在寫死的 (0,0) 預設值，UpdateLayeredWindow 回報成功但畫面
  // 上完全看不到東西。改成：只要目前還沒顯示，不管收到的是 Begin 還是
  // Update，都當第一次顯示處理；已經顯示中才走原本 Update 只重繪的路徑。
  if (kind == EventKind::Begin || !IsOverlayVisible()) {
    ShowOverlayFor(focus, state);
  } else {
    UpdateOverlay(state);
  }
}

class UIElementSink : public ITfUIElementSink {
public:
  UIElementSink() : m_refCount(1) {}

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ITfUIElementSink) {
      *ppv = static_cast<ITfUIElementSink *>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refCount); }
  STDMETHODIMP_(ULONG) Release() override {
    LONG r = InterlockedDecrement(&m_refCount);
    if (r == 0) delete this;
    return r;
  }

  // ITfUIElementSink
  STDMETHODIMP BeginUIElement(DWORD dwUIElementId, BOOL *pbShow) override {
    // 我們自己畫 → 告訴 OS 不要顯示預設 candidate UI。
    if (pbShow) *pbShow = FALSE;
    HandleEvent(dwUIElementId, EventKind::Begin);
    return S_OK;
  }
  STDMETHODIMP UpdateUIElement(DWORD dwUIElementId) override {
    HandleEvent(dwUIElementId, EventKind::Update);
    return S_OK;
  }
  STDMETHODIMP EndUIElement(DWORD dwUIElementId) override {
    HandleEvent(dwUIElementId, EventKind::End);
    return S_OK;
  }

private:
  LONG m_refCount;
};

} // namespace

void TsfSetLastFocusEdit(HWND h) { g_lastFocusEdit.store(h); }

HRESULT TsfInit() {
  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  // S_OK/S_FALSE/RPC_E_CHANGED_MODE 都當成 OK —— STA 已 init 過也能繼續。
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    IME_LOG("[tsf] CoInitializeEx hr=0x%08X", (unsigned)hr);
    return hr;
  }

  ITfThreadMgr *threadMgr = nullptr;
  hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                        (void **)&threadMgr);
  if (FAILED(hr) || !threadMgr) {
    IME_LOG("[tsf] CoCreateInstance(ThreadMgr) hr=0x%08X", (unsigned)hr);
    return hr;
  }

  TfClientId clientId = 0;
  hr = threadMgr->Activate(&clientId);
  if (FAILED(hr)) {
    IME_LOG("[tsf] ThreadMgr Activate hr=0x%08X", (unsigned)hr);
    threadMgr->Release();
    return hr;
  }
  IME_LOG("[tsf] ThreadMgr activated client_id=%lu", (unsigned long)clientId);

  ITfSource *source = nullptr;
  hr = threadMgr->QueryInterface(IID_ITfSource, (void **)&source);
  if (FAILED(hr) || !source) {
    IME_LOG("[tsf] QI ITfSource hr=0x%08X", (unsigned)hr);
    threadMgr->Release();
    return hr;
  }

  ITfUIElementMgr *uiMgr = nullptr;
  hr = threadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&uiMgr);
  if (FAILED(hr) || !uiMgr) {
    IME_LOG("[tsf] QI ITfUIElementMgr hr=0x%08X", (unsigned)hr);
    source->Release();
    threadMgr->Release();
    return hr;
  }

  UIElementSink *sink = new UIElementSink();
  DWORD cookie = 0;
  hr = source->AdviseSink(IID_ITfUIElementSink, sink, &cookie);
  sink->Release(); // AdviseSink 內部會 AddRef，這裡放掉我們建構時的初始 refcount

  if (FAILED(hr)) {
    IME_LOG("[tsf] AdviseSink hr=0x%08X", (unsigned)hr);
    uiMgr->Release();
    source->Release();
    threadMgr->Release();
    return hr;
  }

  g_uiElementMgr = uiMgr;   // 故意不 Release，thread 存活期間持有
  g_threadMgr = threadMgr;  // 同上
  source->Release();

  IME_LOG("[tsf] UIElementSink advised, cookie=0x%lX", (unsigned long)cookie);
  return S_OK;
}
