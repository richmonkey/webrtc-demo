/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vcm_capturer.h"

#include <stdint.h>

#include <memory>

#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"

using webrtc::VideoFrame;
using webrtc::VideoCaptureFactory;
using webrtc::I420Buffer;

TestVideoCapturer::~TestVideoCapturer() = default;

void TestVideoCapturer::OnFrame(const VideoFrame& original_frame) {
  int cropped_width = 0;
  int cropped_height = 0;
  int out_width = 0;
  int out_height = 0;

  VideoFrame frame = MaybePreprocess(original_frame);

  if (!video_adapter_.AdaptFrameResolution(
          frame.width(), frame.height(), frame.timestamp_us() * 1000,
          &cropped_width, &cropped_height, &out_width, &out_height)) {
    // Drop frame in order to respect frame rate constraint.
    return;
  }

  if (out_height != frame.height() || out_width != frame.width()) {
    // Video adapter has requested a down-scale. Allocate a new buffer and
    // return scaled version.
    // For simplicity, only scale here without cropping.
    rtc::scoped_refptr<I420Buffer> scaled_buffer =
        I420Buffer::Create(out_width, out_height);
    scaled_buffer->ScaleFrom(*frame.video_frame_buffer()->ToI420());
    VideoFrame::Builder new_frame_builder =
        VideoFrame::Builder()
            .set_video_frame_buffer(scaled_buffer)
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(frame.timestamp_us())
            .set_id(frame.id());
    if (frame.has_update_rect()) {
      VideoFrame::UpdateRect new_rect = frame.update_rect().ScaleWithFrame(
          frame.width(), frame.height(), 0, 0, frame.width(), frame.height(),
          out_width, out_height);
      new_frame_builder.set_update_rect(new_rect);
    }
    broadcaster_.OnFrame(new_frame_builder.build());

  } else {
    // No adaptations needed, just return the frame as is.
    broadcaster_.OnFrame(frame);
  }
}

rtc::VideoSinkWants TestVideoCapturer::GetSinkWants() {
  return broadcaster_.wants();
}

void TestVideoCapturer::AddOrUpdateSink(
    rtc::VideoSinkInterface<VideoFrame>* sink,
    const rtc::VideoSinkWants& wants) {
  broadcaster_.AddOrUpdateSink(sink, wants);
  UpdateVideoAdapter();
}

void TestVideoCapturer::RemoveSink(rtc::VideoSinkInterface<VideoFrame>* sink) {
  broadcaster_.RemoveSink(sink);
  UpdateVideoAdapter();
}

void TestVideoCapturer::UpdateVideoAdapter() {
  video_adapter_.OnSinkWants(broadcaster_.wants());
}

VideoFrame TestVideoCapturer::MaybePreprocess(const VideoFrame& frame) {
  MutexLock lock(&lock_);
  if (preprocessor_ != nullptr) {
    return preprocessor_->Preprocess(frame);
  } else {
    return frame;
  }
}


    
VcmCapturer::VcmCapturer() : vcm_(nullptr) {}

bool VcmCapturer::Init(size_t width,
                       size_t height,
                       size_t target_fps,
                       size_t capture_device_index) {
  std::unique_ptr<VideoCaptureModule::DeviceInfo> device_info(
      VideoCaptureFactory::CreateDeviceInfo());

  char device_name[256];
  char unique_name[256];
  if (device_info->GetDeviceName(static_cast<uint32_t>(capture_device_index),
                                 device_name, sizeof(device_name), unique_name,
                                 sizeof(unique_name)) != 0) {
    Destroy();
    return false;
  }

  vcm_ = webrtc::VideoCaptureFactory::Create(unique_name);
  if (!vcm_) {
    return false;
  }
  vcm_->RegisterCaptureDataCallback(this);

  device_info->GetCapability(vcm_->CurrentDeviceName(), 0, capability_);

  capability_.width = static_cast<int32_t>(width);
  capability_.height = static_cast<int32_t>(height);
  capability_.maxFPS = static_cast<int32_t>(target_fps);
  capability_.videoType = webrtc::VideoType::kI420;

  if (vcm_->StartCapture(capability_) != 0) {
    Destroy();
    return false;
  }

  RTC_CHECK(vcm_->CaptureStarted());

  return true;
}

VcmCapturer* VcmCapturer::Create(size_t width,
                                 size_t height,
                                 size_t target_fps,
                                 size_t capture_device_index) {
  std::unique_ptr<VcmCapturer> vcm_capturer(new VcmCapturer());
  if (!vcm_capturer->Init(width, height, target_fps, capture_device_index)) {
    RTC_LOG(LS_WARNING) << "Failed to create VcmCapturer(w = " << width
                        << ", h = " << height << ", fps = " << target_fps
                        << ")";
    return nullptr;
  }
  return vcm_capturer.release();
}

void VcmCapturer::Destroy() {
  if (!vcm_)
    return;

  vcm_->StopCapture();
  vcm_->DeRegisterCaptureDataCallback();
  // Release reference to VCM.
  vcm_ = nullptr;
}

VcmCapturer::~VcmCapturer() {
  Destroy();
}

void VcmCapturer::OnFrame(const VideoFrame& frame) {
  TestVideoCapturer::OnFrame(frame);
}

