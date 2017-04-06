/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_EXAMPLES_VOIP_NET_CAPTURER_H_
#define WEBRTC_EXAMPLES_VOIP_NET_CAPTURER_H_


#include "webrtc/api/video/video_frame.h"
#include "webrtc/common_video/include/i420_buffer_pool.h"
#include "webrtc/media/base/videocapturer.h"

#include <memory>

#include "webrtc/base/platform_thread.h"
#include "webrtc/common_types.h"


namespace rtc {
class Thread;
}  // namespace rtc

namespace webrtc {
    
struct VideoCaptureCapability;
class CriticalSectionWrapper;
 
class NetCapturer : public cricket::VideoCapturer {
 public:
  NetCapturer();
  ~NetCapturer();

  cricket::CaptureState Start(const cricket::VideoFormat& format) override;
  void Stop() override;
  bool IsRunning() override;
  bool IsScreencast() const override {
    return false;
  }
  bool GetPreferredFourccs(std::vector<uint32_t> *fourccs) override {
      fourccs->push_back(cricket::FOURCC_YV12);
      fourccs->push_back(cricket::FOURCC_YUY2);
      fourccs->push_back(cricket::FOURCC_UYVY);
      fourccs->push_back(cricket::FOURCC_NV12);
      fourccs->push_back(cricket::FOURCC_NV21);
      fourccs->push_back(cricket::FOURCC_MJPG);
      fourccs->push_back(cricket::FOURCC_ARGB);
      fourccs->push_back(cricket::FOURCC_24BG);
      return true;
  }

 private:
  std::set<cricket::VideoFormat> FillCapabilities(int fd);
    
    
 private:
  enum {kNoOfV4L2Bufffers=4};

  static bool CaptureThread(void*);
  bool CaptureProcess();
  bool AllocateVideoBuffers();
  bool DeAllocateVideoBuffers();

  int32_t IncomingFrame(
    uint8_t* videoFrame,
    size_t videoFrameLength,
    const VideoCaptureCapability& frameInfo,
    int64_t captureTime/*=0*/);
      
  // TODO(pbos): Stop using unique_ptr and resetting the thread.
  std::unique_ptr<rtc::PlatformThread> _captureThread;
  CriticalSectionWrapper* _captureCritSect;

  CriticalSectionWrapper *_apiCs;
    
  int32_t _deviceId;
  int32_t _deviceFd;

  int32_t _buffersAllocatedByDevice;
  int32_t _currentWidth;
  int32_t _currentHeight;
  int32_t _currentFrameRate;
  bool _captureStarted;
  RawVideoType _captureVideoType;

    
}; 


}  // namespace webrtc

#endif  // WEBRTC_API_OBJC_AVFOUNDATION_VIDEO_CAPTURER_H_
