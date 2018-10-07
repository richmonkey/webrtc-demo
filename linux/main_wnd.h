/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXAMPLES_PEERCONNECTION_CLIENT_LINUX_MAIN_WND_H_
#define EXAMPLES_PEERCONNECTION_CLIENT_LINUX_MAIN_WND_H_

#include <memory>
#include <string>

#include "examples/voip/voip_wnd.h"
#include "examples/voip/peer_connection_client.h"

// Forward declarations.
typedef struct _GtkWidget GtkWidget;
typedef union _GdkEvent GdkEvent;
typedef struct _GdkEventKey GdkEventKey;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreePath GtkTreePath;
typedef struct _GtkTreeViewColumn GtkTreeViewColumn;
typedef struct _cairo cairo_t;

// Implements the main UI of the peer connection client.
// This is functionally equivalent to the MainWnd class in the Windows
// implementation.
class GtkMainWnd : public VOIPWnd {
 public:
  GtkMainWnd(PeerConnectionClient *client, rtc::Thread* main_thread,
             int64_t uid, std::string& token);
  ~GtkMainWnd();

  virtual bool IsWindow();
  virtual void SwitchToConnectUI();
  virtual void SwitchToStreamingUI();
  virtual void MessageBox(const char* caption, const char* text, bool is_error);
  
  virtual void StartLocalRenderer();
  virtual void StopLocalRenderer();
  virtual void StartRemoteRenderer();
  virtual void StopRemoteRenderer();

  virtual void QueueUIThreadCallback(int msg_id, void* data);

  // Creates and shows the main window with the |Connect UI| enabled.
  bool Create();

  // Destroys the window.  When the window is destroyed, it ends the
  // main message loop.
  bool Destroy();

  // Callback for when the main window is destroyed.
  void OnDestroyed(GtkWidget* widget, GdkEvent* event);

  // Callback for when the user clicks the "Connect" button.
  void OnClicked(GtkWidget* widget);

  // Callback for keystrokes.  Used to capture Esc and Return.
  void OnKeyPress(GtkWidget* widget, GdkEventKey* key);


  void OnRedraw();

  void Draw(GtkWidget* widget, cairo_t* cr);

 protected:
  class VideoRenderer : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
   public:
    VideoRenderer(GtkMainWnd* main_wnd);
    virtual ~VideoRenderer();

    // VideoSinkInterface implementation
    void OnFrame(const webrtc::VideoFrame& frame) override;

    const uint8_t* image() const { return image_.get(); }

    int width() const { return width_; }

    int height() const { return height_; }

   protected:
    void SetSize(int width, int height);
    std::unique_ptr<uint8_t[]> image_;
    int width_;
    int height_;
    GtkMainWnd* main_wnd_;
    rtc::scoped_refptr<webrtc::VideoTrackInterface> rendered_track_;
  };

 rtc::VideoSinkInterface<webrtc::VideoFrame> *localRender() {
     return local_renderer_.get();
 }
  rtc::VideoSinkInterface<webrtc::VideoFrame> *remoteRender() {
      return remote_renderer_.get();
  }

    virtual void OnPeerConnected();
    virtual void OnPeerDisconnected();

    

 protected:
  GtkWidget* window_;     // Our main window.
  GtkWidget* draw_area_;  // The drawing surface for rendering video streams.
  GtkWidget* vbox_;       // Container for the Connect UI.
  GtkWidget* server_edit_;
  GtkWidget* port_edit_;
  GtkWidget* peer_list_;  // The list of peers.
  std::string server_;
  std::string port_;
  bool autoconnect_;
  bool autocall_;
  std::unique_ptr<VideoRenderer> local_renderer_;
  std::unique_ptr<VideoRenderer> remote_renderer_;
  int width_;
  int height_;
  std::unique_ptr<uint8_t[]> draw_buffer_;
  int draw_buffer_size_;
};

#endif  // EXAMPLES_PEERCONNECTION_CLIENT_LINUX_MAIN_WND_H_
