/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/voip/linux/main_wnd.h"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <stddef.h>

#include "api/video/i420_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
//#include "rtc_base/strings/stringutils.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"


#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "api/video/video_source_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"

#include "examples/voip/defaults.h"

//using rtc::sprintfn;

namespace {

//
// Simple static functions that simply forward the callback to the
// GtkMainWnd instance.
//

gboolean OnDestroyedCallback(GtkWidget* widget,
                             GdkEvent* event,
                             gpointer data) {
  reinterpret_cast<GtkMainWnd*>(data)->OnDestroyed(widget, event);
  return FALSE;
}

void OnClickedCallback(GtkWidget* widget, gpointer data) {
  reinterpret_cast<GtkMainWnd*>(data)->OnClicked(widget);
}


gboolean OnKeyPressCallback(GtkWidget* widget,
                            GdkEventKey* key,
                            gpointer data) {
  reinterpret_cast<GtkMainWnd*>(data)->OnKeyPress(widget, key);
  return false;
}


struct UIThreadCallbackData {
  explicit UIThreadCallbackData(int id, void* d)
      : msg_id(id), data(d) {}
  int msg_id;
  void* data;
};

gboolean HandleUIThreadCallback(gpointer data) {
  UIThreadCallbackData* cb_data = reinterpret_cast<UIThreadCallbackData*>(data);
  //cb_data->callback->UIThreadCallback(cb_data->msg_id, cb_data->data);
  delete cb_data;
  return false;
}


gboolean Redraw(gpointer data) {
  GtkMainWnd* wnd = reinterpret_cast<GtkMainWnd*>(data);
  wnd->OnRedraw();
  return false;
}

gboolean Draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
  GtkMainWnd* wnd = reinterpret_cast<GtkMainWnd*>(data);
  wnd->Draw(widget, cr);
  return false;
}

}  // namespace

//
// GtkMainWnd implementation.
//

GtkMainWnd::GtkMainWnd(PeerConnectionClient *client, rtc::Thread* main_thread,
                       int64_t uid, std::string& token)
    : VOIPWnd(client, main_thread, uid, token),
      window_(NULL),
      draw_area_(NULL),
      vbox_(NULL),
      server_edit_(NULL),
      port_edit_(NULL),
      peer_list_(NULL),
      autoconnect_(false),
      autocall_(false) {
}

GtkMainWnd::~GtkMainWnd() {
  RTC_DCHECK(!IsWindow());
}



bool GtkMainWnd::IsWindow() {
  return window_ != NULL && GTK_IS_WINDOW(window_);
}

void GtkMainWnd::MessageBox(const char* caption,
                            const char* text,
                            bool is_error) {
  GtkWidget* dialog = gtk_message_dialog_new(
      GTK_WINDOW(window_), GTK_DIALOG_DESTROY_WITH_PARENT,
      is_error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s",
      text);
  gtk_window_set_title(GTK_WINDOW(dialog), caption);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}


void GtkMainWnd::StartLocalRenderer() {
  local_renderer_.reset(new VideoRenderer(this));
}

void GtkMainWnd::StopLocalRenderer() {
  local_renderer_.reset();
}

void GtkMainWnd::StartRemoteRenderer() {
  remote_renderer_.reset(new VideoRenderer(this));
}

void GtkMainWnd::StopRemoteRenderer() {
  remote_renderer_.reset();
}

void GtkMainWnd::QueueUIThreadCallback(int msg_id, void* data) {
  g_idle_add(HandleUIThreadCallback,
             new UIThreadCallbackData(msg_id, data));
}

bool GtkMainWnd::Create() {
  RTC_DCHECK(window_ == NULL);

  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  if (window_) {
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window_), 640, 480);
    gtk_window_set_title(GTK_WINDOW(window_), "PeerConnection client");
    g_signal_connect(G_OBJECT(window_), "delete-event",
                     G_CALLBACK(&OnDestroyedCallback), this);
    g_signal_connect(window_, "key-press-event", G_CALLBACK(OnKeyPressCallback),
                     this);

    SwitchToConnectUI();
  }

  return window_ != NULL;
}

bool GtkMainWnd::Destroy() {
  if (!IsWindow())
    return false;

  gtk_widget_destroy(window_);
  window_ = NULL;

  return true;
}

void GtkMainWnd::OnPeerConnected() {
    SwitchToStreamingUI();    
    StartLocalRenderer();
    StartRemoteRenderer();

    VOIPWnd::OnPeerConnected();
}
void GtkMainWnd::OnPeerDisconnected() {
    VOIPWnd::OnPeerDisconnected();
    StopLocalRenderer();
    StopRemoteRenderer();
    SwitchToConnectUI();
}


void GtkMainWnd::SwitchToConnectUI() {
  RTC_LOG(INFO) << __FUNCTION__;

  RTC_DCHECK(IsWindow());
  RTC_DCHECK(vbox_ == NULL);

  gtk_container_set_border_width(GTK_CONTAINER(window_), 10);


  if (draw_area_) {
      gtk_widget_destroy(draw_area_);
      draw_area_ = NULL;
      draw_buffer_.reset();
  }  

#if GTK_MAJOR_VERSION == 2
  vbox_ = gtk_vbox_new(FALSE, 5);
#else
  vbox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
#endif
  GtkWidget* valign = gtk_alignment_new(0, 1, 0, 0);
  gtk_container_add(GTK_CONTAINER(vbox_), valign);
  gtk_container_add(GTK_CONTAINER(window_), vbox_);

#if GTK_MAJOR_VERSION == 2
  GtkWidget* hbox = gtk_hbox_new(FALSE, 5);
#else
  GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
#endif

  GtkWidget* label = gtk_label_new("Server");
  gtk_container_add(GTK_CONTAINER(hbox), label);

  server_edit_ = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(server_edit_), server_.c_str());
  gtk_widget_set_size_request(server_edit_, 400, 30);
  gtk_container_add(GTK_CONTAINER(hbox), server_edit_);

  port_edit_ = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(port_edit_), port_.c_str());
  gtk_widget_set_size_request(port_edit_, 70, 30);
  gtk_container_add(GTK_CONTAINER(hbox), port_edit_);

  GtkWidget* button = gtk_button_new_with_label("Connect");
  gtk_widget_set_size_request(button, 70, 30);
  g_signal_connect(button, "clicked", G_CALLBACK(OnClickedCallback), this);
  gtk_container_add(GTK_CONTAINER(hbox), button);

  GtkWidget* halign = gtk_alignment_new(1, 0, 0, 0);
  gtk_container_add(GTK_CONTAINER(halign), hbox);
  gtk_box_pack_start(GTK_BOX(vbox_), halign, FALSE, FALSE, 0);

  gtk_widget_show_all(window_);
  
}

void GtkMainWnd::SwitchToStreamingUI() {
  RTC_LOG(INFO) << __FUNCTION__;

  RTC_DCHECK(draw_area_ == NULL);

  gtk_container_set_border_width(GTK_CONTAINER(window_), 0);
  
  if (vbox_) {
      gtk_widget_destroy(vbox_);
      vbox_ = NULL;
      server_edit_ = NULL;
      port_edit_ = NULL;
  }
  
  draw_area_ = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(window_), draw_area_);
  g_signal_connect(G_OBJECT(draw_area_), "draw", G_CALLBACK(&::Draw), this);

  gtk_widget_show_all(window_);
}

void GtkMainWnd::OnDestroyed(GtkWidget* widget, GdkEvent* event) {
  window_ = NULL;
  draw_area_ = NULL;
  vbox_ = NULL;
  server_edit_ = NULL;
  port_edit_ = NULL;
  peer_list_ = NULL;
}

void GtkMainWnd::OnClicked(GtkWidget* widget) {
  // Make the connect button insensitive, so that it cannot be clicked more than
  // once.  Now that the connection includes auto-retry, it should not be
  // necessary to click it more than once.
  gtk_widget_set_sensitive(widget, false);
  server_ = gtk_entry_get_text(GTK_ENTRY(server_edit_));
  port_ = gtk_entry_get_text(GTK_ENTRY(port_edit_));
  //int port = port_.length() ? atoi(port_.c_str()) : 0;
  //callback_->StartLogin(server_, port);
  Dial();
}

void GtkMainWnd::OnKeyPress(GtkWidget* widget, GdkEventKey* key) {
  if (key->type == GDK_KEY_PRESS) {
    switch (key->keyval) {
#if GTK_MAJOR_VERSION == 2
      case GDK_Escape:
#else
      case GDK_KEY_Escape:
#endif
        if (draw_area_) {
            //callback_->DisconnectFromCurrentPeer();
        } else if (peer_list_) {
            //callback_->DisconnectFromServer();
        }
        break;

#if GTK_MAJOR_VERSION == 2
      case GDK_KP_Enter:
      case GDK_Return:
#else
      case GDK_KEY_KP_Enter:
      case GDK_KEY_Return:
#endif
        if (vbox_) {
          OnClicked(NULL);
        } else if (peer_list_) {
          // OnRowActivated will be called automatically when the user
          // presses enter.
        }
        break;

      default:
        break;
    }
  }
}


void GtkMainWnd::OnRedraw() {
  gdk_threads_enter();

  VideoRenderer* remote_renderer = remote_renderer_.get();
  if (remote_renderer && remote_renderer->image() != NULL &&
      draw_area_ != NULL) {
    width_ = remote_renderer->width();
    height_ = remote_renderer->height();

    if (!draw_buffer_.get()) {
      draw_buffer_size_ = (width_ * height_ * 4) * 4;
      draw_buffer_.reset(new uint8_t[draw_buffer_size_]);
      gtk_widget_set_size_request(draw_area_, width_ * 2, height_ * 2);
    }
    
    if (draw_buffer_size_ != (width_ * height_ * 4) * 4) {
        draw_buffer_size_ = (width_ * height_ * 4) * 4;
        draw_buffer_.reset(new uint8_t[draw_buffer_size_]);
        gtk_widget_set_size_request(draw_area_, width_ * 2, height_ * 2);
    }

    const uint32_t* image =
        reinterpret_cast<const uint32_t*>(remote_renderer->image());
    uint32_t* scaled = reinterpret_cast<uint32_t*>(draw_buffer_.get());
    for (int r = 0; r < height_; ++r) {
      for (int c = 0; c < width_; ++c) {
        int x = c * 2;
        scaled[x] = scaled[x + 1] = image[c];
      }

      uint32_t* prev_line = scaled;
      scaled += width_ * 2;
      memcpy(scaled, prev_line, (width_ * 2) * 4);

      image += width_;
      scaled += width_ * 2;
    }

    VideoRenderer* local_renderer = local_renderer_.get();
    if (local_renderer && local_renderer->image()) {
      image = reinterpret_cast<const uint32_t*>(local_renderer->image());
      scaled = reinterpret_cast<uint32_t*>(draw_buffer_.get());
      #if 0
      // Position the local preview on the right side.
      scaled += (width_ * 2) - (local_renderer->width() / 2);
      // right margin...
      scaled -= 10;
      // ... towards the bottom.
      scaled += (height_ * width_ * 4) - ((local_renderer->height() / 2) *
                                          (local_renderer->width() / 2) * 4);
      // bottom margin...
      scaled -= (width_ * 2) * 5;
      #endif
      for (int r = 0; r < local_renderer->height(); r += 2) {
        for (int c = 0; c < local_renderer->width(); c += 2) {
          scaled[c / 2] = image[c + r * local_renderer->width()];
        }
        scaled += width_ * 2;
      }
    }

#if GTK_MAJOR_VERSION == 2
    gdk_draw_rgb_32_image(draw_area_->window,
                          draw_area_->style->fg_gc[GTK_STATE_NORMAL], 0, 0,
                          width_ * 2, height_ * 2, GDK_RGB_DITHER_MAX,
                          draw_buffer_.get(), (width_ * 2) * 4);
#else
    gtk_widget_queue_draw(draw_area_);
#endif
  }

  gdk_threads_leave();
}

void GtkMainWnd::Draw(GtkWidget* widget, cairo_t* cr) {
#if GTK_MAJOR_VERSION != 2
  cairo_format_t format = CAIRO_FORMAT_RGB24;
  cairo_surface_t* surface = cairo_image_surface_create_for_data(
      draw_buffer_.get(), format, width_ * 2, height_ * 2,
      cairo_format_stride_for_width(format, width_ * 2));
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_rectangle(cr, 0, 0, width_ * 2, height_ * 2);
  cairo_fill(cr);
  cairo_surface_destroy(surface);
#else
  RTC_NOTREACHED();
#endif
}

GtkMainWnd::VideoRenderer::VideoRenderer(
    GtkMainWnd* main_wnd)
    : width_(0),
      height_(0),
      main_wnd_(main_wnd) {
}

GtkMainWnd::VideoRenderer::~VideoRenderer() {

}

void GtkMainWnd::VideoRenderer::SetSize(int width, int height) {
  gdk_threads_enter();

  if (width_ == width && height_ == height) {
    return;
  }

  width_ = width;
  height_ = height;
  image_.reset(new uint8_t[width * height * 4]);
  gdk_threads_leave();
}

void GtkMainWnd::VideoRenderer::OnFrame(const webrtc::VideoFrame& video_frame) {
  gdk_threads_enter();

  RTC_LOG(INFO) << "video render on frame";
  
  rtc::scoped_refptr<webrtc::I420BufferInterface> buffer(
      video_frame.video_frame_buffer()->ToI420());
  if (video_frame.rotation() != webrtc::kVideoRotation_0) {
    buffer = webrtc::I420Buffer::Rotate(*buffer, video_frame.rotation());
  }
  SetSize(buffer->width(), buffer->height());

  // The order in the name of libyuv::I420To(ABGR,RGBA) is ambiguous because
  // it doesn't tell you if it is referring to how it is laid out in memory as
  // bytes or if endiannes is taken into account.
  // This was supposed to be a call to libyuv::I420ToRGBA but it was resulting
  // in a reddish video output (see https://bugs.webrtc.org/6857) because it
  // was producing an unexpected byte order (ABGR, byte swapped).
  libyuv::I420ToARGB(buffer->DataY(), buffer->StrideY(), buffer->DataU(),
                     buffer->StrideU(), buffer->DataV(), buffer->StrideV(),
                     image_.get(), width_ * 4, buffer->width(),
                     buffer->height());

  gdk_threads_leave();

  g_idle_add(Redraw, main_wnd_);
}
