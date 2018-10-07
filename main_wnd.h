/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_MAIN_WND_H_
#define WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_MAIN_WND_H_

#include <map>
#include <memory>
#include <string>

#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/video/video_frame.h"
#include "webrtc/base/win32.h"
#include "webrtc/base/signalthread.h"
#include "webrtc/base/sigslot.h"
#include "webrtc/base/json.h"
#include "webrtc/media/base/mediachannel.h"
#include "webrtc/media/base/videocommon.h"
#include "webrtc/examples/im_client/peer_connection_client.h"
#include "webrtc/examples/im_client/video_renderer.h"
#include "webrtc/examples/im_client/conductor.h"


class MainWnd : public VOIPWnd {
 public:
  static const wchar_t kClassName[];

  enum WindowMessages {
    UI_THREAD_CALLBACK = WM_APP + 1,
  };
  
  enum UI {
    CONNECT_TO_SERVER,
    LIST_PEERS,
    STREAMING,
  };


  MainWnd(PeerConnectionClient* client,
          rtc::Thread* main_thread,
          int64_t uid, std::string& token);
  ~MainWnd();

  
  bool Create();
  bool Destroy();
  bool PreTranslateMessage(MSG* msg);

  virtual bool IsWindow();
  virtual void SwitchToConnectUI();
  virtual void SwitchToStreamingUI();
  virtual void MessageBox(const char* caption, const char* text,
                          bool is_error);
  virtual UI current_ui() { return ui_; }

 protected:
  virtual void OnPeerConnected();
  virtual void OnPeerDisconnected();

  
 protected:
  enum ChildWindowID {
    EDIT_ID = 1,
    BUTTON_ID,
    LABEL1_ID,
    LABEL2_ID,
    LISTBOX_ID,
  };


  HWND handle() const { return wnd_; }

  void OnPaint();
  
  void OnDestroyed();

  void OnDefaultAction();

  bool OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static bool RegisterWindowClass();

  void CreateChildWindow(HWND* wnd, ChildWindowID id, const wchar_t* class_name,
                         DWORD control_style, DWORD ex_style);
  void CreateChildWindows();

  void LayoutConnectUI(bool show);
  void LayoutPeerListUI(bool show);

  

 
 private:
  UI ui_;
  
  DWORD ui_thread_id_;
  HWND edit1_;
  HWND edit2_;
  HWND label1_;
  HWND label2_;
  HWND button_;
  HWND listbox_;
  bool destroyed_;
  void* nested_msg_;
  static ATOM wnd_class_;
  std::string server_;
  std::string port_;
  bool auto_connect_;
  bool auto_call_;

   
  HWND wnd_;   
  
};


#endif  // WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_MAIN_WND_H_
