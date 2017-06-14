/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/examples/im_client/main_wnd.h"

#include <math.h>

#include "libyuv/convert_argb.h"
#include "webrtc/api/video/i420_buffer.h"
#include "webrtc/base/arraysize.h"
#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/examples/im_client/conductor.h"

#include <rpc.h>

ATOM MainWnd::wnd_class_ = 0;
const wchar_t MainWnd::kClassName[] = L"WebRTC_MainWnd";

using rtc::sprintfn;

namespace {
const int kDialDelay = 1000;
const int kPingDelay = 1000;
    
const char kConnecting[] = "Connecting... ";
const char kNoVideoStreams[] = "(no video streams either way)";
const char kNoIncomingStream[] = "(no incoming video)";

void CalculateWindowSizeForText(HWND wnd, const wchar_t* text,
                                size_t* width, size_t* height) {
  HDC dc = ::GetDC(wnd);
  RECT text_rc = {0};
  ::DrawText(dc, text, -1, &text_rc, DT_CALCRECT | DT_SINGLELINE);
  ::ReleaseDC(wnd, dc);
  RECT client, window;
  ::GetClientRect(wnd, &client);
  ::GetWindowRect(wnd, &window);

  *width = text_rc.right - text_rc.left;
  *width += (window.right - window.left) -
            (client.right - client.left);
  *height = text_rc.bottom - text_rc.top;
  *height += (window.bottom - window.top) -
             (client.bottom - client.top);
}

HFONT GetDefaultFont() {
  static HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  return font;
}

std::string GetWindowText(HWND wnd) {
  char text[MAX_PATH] = {0};
  ::GetWindowTextA(wnd, &text[0], ARRAYSIZE(text));
  return text;
}

void AddListBoxItem(HWND listbox, const std::string& str, LPARAM item_data) {
  LRESULT index = ::SendMessageA(listbox, LB_ADDSTRING, 0,
      reinterpret_cast<LPARAM>(str.c_str()));
  ::SendMessageA(listbox, LB_SETITEMDATA, index, item_data);
}

}  // namespace



//voipwnd
VOIPWnd::VOIPWnd(PeerConnectionClient *client, rtc::Thread* main_thread,
                 int64_t uid, std::string& token)
    :state_(0), client_(client),
     uid_(uid), token_(token),
     main_thread_(main_thread),
     wnd_(NULL) {
    client_->RegisterObserver(this);
}

VOIPWnd::~VOIPWnd() {
    client_->RegisterObserver(NULL);
}

void VOIPWnd::OnMessage(rtc::Message* msg) {
    if (msg->message_id == 1) {
        if (state_ == VOIP_DIALING) {
            SendVOIPCommand(peer_id_, VOIP_COMMAND_DIAL_VIDEO, channel_id_);
            rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kDialDelay,
                                                this, 1);
        }
    } else if (msg->message_id == 2) {
        if (state_ == VOIP_CONNECTED) {
            uint32_t now = rtc::Time32();
            if (now - timestamp_ > 10*1000) {
                LOG(INFO) << "peer:" << peer_id_ << " timeout";
                OnPeerDisconnected();
                return;
            }
           
            LOG(INFO) << "send ping command";
            SendVOIPCommand(peer_id_, VOIP_COMMAND_PING, channel_id_);
            rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay,
                                                this, 2);           
        }
    }
}

void VOIPWnd::OnPeerConnected() {
    //on peer connected
    timestamp_ = rtc::Time32();
    conductor_ = new rtc::RefCountedObject<Conductor>(client_, handle(),
                                                      main_thread_, uid_, token_);
    conductor_->AddRef();
    conductor_->ConnectToPeer(peer_id_);
}

void VOIPWnd::OnPeerDisconnected() {
    conductor_->OnPeerDisconnected(peer_id_);
    conductor_->Release();
    conductor_  = NULL;
    state_ = VOIP_HANGED_UP;
    LOG(INFO) << "peer:" << peer_id_ << " disconnected";    
}

void VOIPWnd::HandleVOIPMessage(int64_t sender, int64_t receiver, Json::Value& obj) {
    int64_t cmd = obj["command"].asInt();
    std::string channel_id = obj["channel_id"].asString();
    LOG(INFO) << "voip:" << cmd << " channel:" << channel_id;


    if (sender != peer_id_) {
        return;
    }
    if (cmd == VOIP_COMMAND_ACCEPT) {
        SendVOIPCommand(peer_id_, VOIP_COMMAND_CONNECTED, channel_id_);
        if (state_ == VOIP_CONNECTED) {
            return;
        }

        state_ = VOIP_CONNECTED;

        rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay,
                                            this, 2);

        OnPeerConnected();
 
    } else if (cmd == VOIP_COMMAND_HANG_UP) {
        if (state_ == VOIP_CONNECTED) {
            OnPeerDisconnected();
        
        }
    } else if (cmd == VOIP_COMMAND_PING) {
        timestamp_ = rtc::Time32();
    }
    return;        
}

void VOIPWnd::HandleP2PMessage(int64_t sender, int64_t receiver, Json::Value& obj) {
    if (state_ == VOIP_CONNECTED) {
        conductor_->OnMessageFromPeer(sender, rtc::JsonValueToString(obj));
    }
}    

void VOIPWnd::HandleRTMessage(int64_t sender, int64_t receiver, std::string& content) {
    Json::Reader reader;
    Json::Value value;

    if (reader.parse(content, value)) {
        Json::Value obj;
        bool r;
        r = rtc::GetValueFromJsonObject(value, "voip", &obj);
        if (r) {
            HandleVOIPMessage(sender, receiver, obj);
        }

        r = rtc::GetValueFromJsonObject(value, "p2p", &obj);
        if (r) {
            LOG(INFO) << "p2p message:" << content;
            HandleP2PMessage(sender, receiver, obj);
        }
    }
}


void VOIPWnd::OnSignedIn() {
    LOG(INFO) << "signed in";
}

void VOIPWnd::OnServerConnectionFailure() {
    LOG(INFO) << "Failed to connect to server";
}

void VOIPWnd::OnDisconnected() {
    LOG(INFO) << "on disconnected";
}





void VOIPWnd::SendVOIPCommand(int64_t peer_id, int voip_cmd,
                              const std::string& channel_id) {
    Json::Value value;
    value["command"] = voip_cmd;
    value["channel_id"] = channel_id;
 
    Json::Value json;
    json["voip"] = value;
    std::string s = rtc::JsonValueToString(json);
    client_->SendRTMessage(peer_id, s);
}


void VOIPWnd::Dial() {
   LOG(INFO) << "connect...";

    if (state_ != VOIP_HANGED_UP && state_ != 0) {
        return;
    }
    
    ::UUID uuid;
    UuidCreate(&uuid);
    char *str;
    UuidToStringA(&uuid, (RPC_CSTR*)&str);
    LOG(INFO) << "uuid:" << str;
    std::string channel_id(str);
    RpcStringFreeA((RPC_CSTR*)&str);

    //todo input by user
    int64_t peer_id = 1;
    SendVOIPCommand(peer_id, VOIP_COMMAND_DIAL_VIDEO, channel_id);

    rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kDialDelay,
                                        this, 1);

    channel_id_ = channel_id;
    peer_id_ = peer_id;
    state_ = VOIP_DIALING;    
}

void VOIPWnd::OnPaint() {
  PAINTSTRUCT ps;
  ::BeginPaint(handle(), &ps);

  RECT rc;
  ::GetClientRect(handle(), &rc);

  VideoRenderer* local_renderer = NULL;
  VideoRenderer* remote_renderer = NULL;  
  if (conductor_) {
      local_renderer = conductor_->GetLocalRenderer();
      remote_renderer = conductor_->GetRemoteRenderer();
  }
  if (remote_renderer && local_renderer) {
    AutoLock<VideoRenderer> local_lock(local_renderer);
    AutoLock<VideoRenderer> remote_lock(remote_renderer);

    const BITMAPINFO& bmi = remote_renderer->bmi();
    int height = abs(bmi.bmiHeader.biHeight);
    int width = bmi.bmiHeader.biWidth;

    const uint8_t* image = remote_renderer->image();
    if (image != NULL) {
      HDC dc_mem = ::CreateCompatibleDC(ps.hdc);
      ::SetStretchBltMode(dc_mem, HALFTONE);

      // Set the map mode so that the ratio will be maintained for us.
      HDC all_dc[] = { ps.hdc, dc_mem };
      for (int i = 0; i < arraysize(all_dc); ++i) {
        SetMapMode(all_dc[i], MM_ISOTROPIC);
        SetWindowExtEx(all_dc[i], width, height, NULL);
        SetViewportExtEx(all_dc[i], rc.right, rc.bottom, NULL);
      }

      HBITMAP bmp_mem = ::CreateCompatibleBitmap(ps.hdc, rc.right, rc.bottom);
      HGDIOBJ bmp_old = ::SelectObject(dc_mem, bmp_mem);

      POINT logical_area = { rc.right, rc.bottom };
      DPtoLP(ps.hdc, &logical_area, 1);

      HBRUSH brush = ::CreateSolidBrush(RGB(0, 0, 0));
      RECT logical_rect = {0, 0, logical_area.x, logical_area.y };
      ::FillRect(dc_mem, &logical_rect, brush);
      ::DeleteObject(brush);

      int x = (logical_area.x / 2) - (width / 2);
      int y = (logical_area.y / 2) - (height / 2);

      StretchDIBits(dc_mem, x, y, width, height,
                    0, 0, width, height, image, &bmi, DIB_RGB_COLORS, SRCCOPY);

      if ((rc.right - rc.left) > 200 && (rc.bottom - rc.top) > 200) {
        const BITMAPINFO& bmi = local_renderer->bmi();
        image = local_renderer->image();
        int thumb_width = bmi.bmiHeader.biWidth / 4;
        int thumb_height = abs(bmi.bmiHeader.biHeight) / 4;
        StretchDIBits(dc_mem,
            logical_area.x - thumb_width - 10,
            logical_area.y - thumb_height - 10,
            thumb_width, thumb_height,
            0, 0, bmi.bmiHeader.biWidth, -bmi.bmiHeader.biHeight,
            image, &bmi, DIB_RGB_COLORS, SRCCOPY);
      }

      BitBlt(ps.hdc, 0, 0, logical_area.x, logical_area.y,
             dc_mem, 0, 0, SRCCOPY);

      // Cleanup.
      ::SelectObject(dc_mem, bmp_old);
      ::DeleteObject(bmp_mem);
      ::DeleteDC(dc_mem);
    } else {
      // We're still waiting for the video stream to be initialized.
      HBRUSH brush = ::CreateSolidBrush(RGB(0, 0, 0));
      ::FillRect(ps.hdc, &rc, brush);
      ::DeleteObject(brush);

      HGDIOBJ old_font = ::SelectObject(ps.hdc, GetDefaultFont());
      ::SetTextColor(ps.hdc, RGB(0xff, 0xff, 0xff));
      ::SetBkMode(ps.hdc, TRANSPARENT);

      std::string text(kConnecting);
      if (!local_renderer->image()) {
        text += kNoVideoStreams;
      } else {
        text += kNoIncomingStream;
      }
      ::DrawTextA(ps.hdc, text.c_str(), -1, &rc,
          DT_SINGLELINE | DT_CENTER | DT_VCENTER);
      ::SelectObject(ps.hdc, old_font);
    }
  } else {
    HBRUSH brush = ::CreateSolidBrush(::GetSysColor(COLOR_WINDOW));
    ::FillRect(ps.hdc, &rc, brush);
    ::DeleteObject(brush);
  }

  ::EndPaint(handle(), &ps);
}


//mainwnd
MainWnd::MainWnd(PeerConnectionClient *client, rtc::Thread* main_thread,
                 int64_t uid, std::string& token)
    : VOIPWnd(client, main_thread, uid, token),
      ui_(CONNECT_TO_SERVER), edit1_(NULL), edit2_(NULL),
      label1_(NULL), label2_(NULL), button_(NULL), listbox_(NULL),
      destroyed_(false), nested_msg_(NULL) {
    server_ = "127.0.0.1";
    port_ = "8080";
}

MainWnd::~MainWnd() {
  RTC_DCHECK(!IsWindow());
}

bool MainWnd::Create() {
  RTC_DCHECK(wnd_ == NULL);
  if (!RegisterWindowClass())
    return false;

  ui_thread_id_ = ::GetCurrentThreadId();
  wnd_ = ::CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, kClassName, L"WebRTC",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      NULL, NULL, GetModuleHandle(NULL), this);

  ::SendMessage(wnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetDefaultFont()),
                TRUE);

  CreateChildWindows();
  SwitchToConnectUI();

  return wnd_ != NULL;
}

bool MainWnd::Destroy() {
  BOOL ret = FALSE;
  if (IsWindow()) {
    ret = ::DestroyWindow(wnd_);
  }

  return ret != FALSE;
}

bool MainWnd::IsWindow() {
  return wnd_ && ::IsWindow(wnd_) != FALSE;
}

bool MainWnd::PreTranslateMessage(MSG* msg) {
  bool ret = false;
  if (msg->message == WM_CHAR) {
  
  } else if (msg->hwnd == NULL && msg->message == UI_THREAD_CALLBACK) {
      ret = true;
  }
  return ret;
}

void MainWnd::SwitchToConnectUI() {
  RTC_DCHECK(IsWindow());
  LayoutPeerListUI(false);
  ui_ = CONNECT_TO_SERVER;
  LayoutConnectUI(true);
  ::SetFocus(edit1_);

  if (auto_connect_)
    ::PostMessage(button_, BM_CLICK, 0, 0);
}

void MainWnd::SwitchToStreamingUI() {
  LayoutConnectUI(false);
  LayoutPeerListUI(false);
  ui_ = STREAMING;
}


void MainWnd::MessageBox(const char* caption, const char* text, bool is_error) {
  DWORD flags = MB_OK;
  if (is_error)
    flags |= MB_ICONERROR;

  ::MessageBoxA(handle(), text, caption, flags);
}



void MainWnd::OnDestroyed() {
  PostQuitMessage(0);
}

void MainWnd::OnDefaultAction() {
    Dial();
}

bool MainWnd::OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
  switch (msg) {
    case WM_ERASEBKGND:
      *result = TRUE;
      return true;

    case WM_PAINT:
      OnPaint();
      return true;

    case WM_SETFOCUS:
      return true;

    case WM_SIZE:
      break;

    case WM_CTLCOLORSTATIC:
      *result = reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
      return true;

    case WM_COMMAND:
      if (button_ == reinterpret_cast<HWND>(lp)) {
        if (BN_CLICKED == HIWORD(wp))
          OnDefaultAction();
      } 
      return true;

    case WM_CLOSE:
      break;
  }
  return false;
}

// static
LRESULT CALLBACK MainWnd::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  MainWnd* me = reinterpret_cast<MainWnd*>(
      ::GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (!me && WM_CREATE == msg) {
    CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
    me = reinterpret_cast<MainWnd*>(cs->lpCreateParams);
    me->wnd_ = hwnd;
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(me));
  }

  LRESULT result = 0;
  if (me) {
    void* prev_nested_msg = me->nested_msg_;
    me->nested_msg_ = &msg;

    bool handled = me->OnMessage(msg, wp, lp, &result);
    if (WM_NCDESTROY == msg) {
      me->destroyed_ = true;
    } else if (!handled) {
      result = ::DefWindowProc(hwnd, msg, wp, lp);
    }

    if (me->destroyed_ && prev_nested_msg == NULL) {
      me->OnDestroyed();
      me->wnd_ = NULL;
      me->destroyed_ = false;
    }

    me->nested_msg_ = prev_nested_msg;
  } else {
    result = ::DefWindowProc(hwnd, msg, wp, lp);
  }

  return result;
}

// static
bool MainWnd::RegisterWindowClass() {
  if (wnd_class_)
    return true;

  WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
  wcex.style = CS_DBLCLKS;
  wcex.hInstance = GetModuleHandle(NULL);
  wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wcex.hCursor = ::LoadCursor(NULL, IDC_ARROW);
  wcex.lpfnWndProc = &WndProc;
  wcex.lpszClassName = kClassName;
  wnd_class_ = ::RegisterClassEx(&wcex);
  RTC_DCHECK(wnd_class_ != 0);
  return wnd_class_ != 0;
}

void MainWnd::CreateChildWindow(HWND* wnd, MainWnd::ChildWindowID id,
                                const wchar_t* class_name, DWORD control_style,
                                DWORD ex_style) {
  if (::IsWindow(*wnd))
    return;

  // Child windows are invisible at first, and shown after being resized.
  DWORD style = WS_CHILD | control_style;
  *wnd = ::CreateWindowEx(ex_style, class_name, L"", style,
                          100, 100, 100, 100, wnd_,
                          reinterpret_cast<HMENU>(id),
                          GetModuleHandle(NULL), NULL);
  RTC_DCHECK(::IsWindow(*wnd) != FALSE);
  ::SendMessage(*wnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetDefaultFont()),
                TRUE);
}

void MainWnd::CreateChildWindows() {
  // Create the child windows in tab order.
  CreateChildWindow(&label1_, LABEL1_ID, L"Static", ES_CENTER | ES_READONLY, 0);
  CreateChildWindow(&edit1_, EDIT_ID, L"Edit",
                    ES_LEFT | ES_NOHIDESEL | WS_TABSTOP, WS_EX_CLIENTEDGE);
  CreateChildWindow(&label2_, LABEL2_ID, L"Static", ES_CENTER | ES_READONLY, 0);
  CreateChildWindow(&edit2_, EDIT_ID, L"Edit",
                    ES_LEFT | ES_NOHIDESEL | WS_TABSTOP, WS_EX_CLIENTEDGE);
  CreateChildWindow(&button_, BUTTON_ID, L"Button", BS_CENTER | WS_TABSTOP, 0);

  CreateChildWindow(&listbox_, LISTBOX_ID, L"ListBox",
                    LBS_HASSTRINGS | LBS_NOTIFY, WS_EX_CLIENTEDGE);

  ::SetWindowTextA(edit1_, server_.c_str());
  ::SetWindowTextA(edit2_, port_.c_str());
}

void MainWnd::LayoutConnectUI(bool show) {
  struct Windows {
    HWND wnd;
    const wchar_t* text;
    size_t width;
    size_t height;
  } windows[] = {
    { label1_, L"Server" },
    { edit1_, L"XXXyyyYYYgggXXXyyyYYYggg" },
    { label2_, L":" },
    { edit2_, L"XyXyX" },
    { button_, L"Connect" },
  };

  if (show) {
    const size_t kSeparator = 5;
    size_t total_width = (ARRAYSIZE(windows) - 1) * kSeparator;

    for (size_t i = 0; i < ARRAYSIZE(windows); ++i) {
      CalculateWindowSizeForText(windows[i].wnd, windows[i].text,
                                 &windows[i].width, &windows[i].height);
      total_width += windows[i].width;
    }

    RECT rc;
    ::GetClientRect(wnd_, &rc);
    size_t x = (rc.right / 2) - (total_width / 2);
    size_t y = rc.bottom / 2;
    for (size_t i = 0; i < ARRAYSIZE(windows); ++i) {
      size_t top = y - (windows[i].height / 2);
      ::MoveWindow(windows[i].wnd, static_cast<int>(x), static_cast<int>(top),
                   static_cast<int>(windows[i].width),
                   static_cast<int>(windows[i].height),
                   TRUE);
      x += kSeparator + windows[i].width;
      if (windows[i].text[0] != 'X')
        ::SetWindowText(windows[i].wnd, windows[i].text);
      ::ShowWindow(windows[i].wnd, SW_SHOWNA);
    }
  } else {
    for (size_t i = 0; i < ARRAYSIZE(windows); ++i) {
      ::ShowWindow(windows[i].wnd, SW_HIDE);
    }
  }
}

void MainWnd::LayoutPeerListUI(bool show) {
  if (show) {
    RECT rc;
    ::GetClientRect(wnd_, &rc);
    ::MoveWindow(listbox_, 0, 0, rc.right, rc.bottom, TRUE);
    ::ShowWindow(listbox_, SW_SHOWNA);
  } else {
    ::ShowWindow(listbox_, SW_HIDE);
    InvalidateRect(wnd_, NULL, TRUE);
  }
}


void MainWnd::OnPeerConnected() {
    VOIPWnd::OnPeerConnected();
    SwitchToStreamingUI();    
}

void MainWnd::OnPeerDisconnected() {
    VOIPWnd::OnPeerDisconnected();
    SwitchToConnectUI();
}
