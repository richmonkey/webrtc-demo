/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/examples/voip/net_capturer.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <new>

#include "webrtc/api/video/video_rotation.h"
#include "webrtc/base/bind.h"
#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/thread.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "webrtc/modules/video_capture/video_capture_defines.h"


#include "webrtc/base/refcount.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/system_wrappers/include/critical_section_wrapper.h"
#include "webrtc/system_wrappers/include/trace.h"


namespace webrtc {


NetCapturer::NetCapturer() :
    _captureCritSect(CriticalSectionWrapper::CreateCriticalSection()),
    _apiCs(CriticalSectionWrapper::CreateCriticalSection()),
    _deviceId(0),
    _deviceFd(-1),
    _buffersAllocatedByDevice(-1),
    _currentWidth(-1),
    _currentHeight(-1),
    _currentFrameRate(-1),
    _captureStarted(false),
    _captureVideoType(kVideoI420) {

    int fd;
    char device[32];
    
    sprintf(device, "/dev/video%d", 0);
    fd = open(device, O_RDONLY);
    if (fd == -1) {
        return;
    }

    std::set<cricket::VideoFormat> supportedFormats = FillCapabilities(fd);
    close(fd);

    std::vector<cricket::VideoFormat> formats;
    formats.assign(supportedFormats.begin(),
                   supportedFormats.end());
    SetSupportedFormats(formats);

    _deviceFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    
    struct sockaddr_in myaddr;


    memset((char *)&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myaddr.sin_port = htons(19999);
    
    sockaddr* addr = reinterpret_cast<sockaddr*>(&myaddr);
    int err = ::bind(_deviceFd, addr, sizeof(myaddr));
    if (err < 0) {
        LOG(INFO) << "bind failed";
        exit(1);
    }
}

NetCapturer::~NetCapturer() {
    close(_deviceFd);
}


    
// This indicates our format preferences and defines a mapping between
// webrtc::RawVideoType (from video_capture_defines.h) to our FOURCCs.
//static kVideoFourCCEntry kSupportedFourCCs[] = {
//  { FOURCC_I420, webrtc::kVideoI420 },   // 12 bpp, no conversion.
//  { FOURCC_YV12, webrtc::kVideoYV12 },   // 12 bpp, no conversion.
//  { FOURCC_YUY2, webrtc::kVideoYUY2 },   // 16 bpp, fast conversion.
//  { FOURCC_UYVY, webrtc::kVideoUYVY },   // 16 bpp, fast conversion.
//  { FOURCC_NV12, webrtc::kVideoNV12 },   // 12 bpp, fast conversion.
//  { FOURCC_NV21, webrtc::kVideoNV21 },   // 12 bpp, fast conversion.
//  { FOURCC_MJPG, webrtc::kVideoMJPEG },  // compressed, slow conversion.
//  { FOURCC_ARGB, webrtc::kVideoARGB },   // 32 bpp, slow conversion.
//  { FOURCC_24BG, webrtc::kVideoRGB24 },  // 24 bpp, slow conversion.
//};

    
std::set<cricket::VideoFormat> NetCapturer::FillCapabilities(int fd)
{
    std::set<cricket::VideoFormat> supportedFormats;
    cricket::VideoFormat format;
    format.width = 640;
    format.height = 480;
    format.fourcc = cricket::FOURCC_I420;
    format.interval = format.FpsToInterval(15);
    supportedFormats.insert(format);
    return supportedFormats;
}

cricket::CaptureState NetCapturer::Start(const cricket::VideoFormat& format) 
{
    if (_captureStarted)
    {
          LOG(LS_ERROR) << "The capturer is already running.";
          return cricket::CaptureState::CS_FAILED;
    }

    CriticalSectionScoped cs(_captureCritSect);
  

    //start capture thread;
    if (!_captureThread)
    {
        _captureThread.reset(new rtc::PlatformThread(
            NetCapturer::CaptureThread, this, "CaptureThread"));
        _captureThread->Start();
        _captureThread->SetPriority(rtc::kHighPriority);
    }

    _currentWidth = format.width;
    _currentHeight = format.height;
    _captureVideoType = kVideoI420;
    
    SetCaptureFormat(&format);
    
    _captureStarted = true;

    SetCaptureState(cricket::CaptureState::CS_RUNNING);

    return cricket::CaptureState::CS_STARTING;
}

void NetCapturer::Stop()
{
    if (_captureThread) {
        // Make sure the capture thread stop stop using the critsect.
        _captureThread->Stop();
        _captureThread.reset();
    }

    CriticalSectionScoped cs(_captureCritSect);
    if (_captureStarted)
    {
        _captureStarted = false;
    }

    SetCaptureFormat(NULL);
}

    

bool NetCapturer::CaptureThread(void* obj)
{
    return static_cast<NetCapturer*> (obj)->CaptureProcess();
}
    
bool NetCapturer::CaptureProcess()
{
    int retVal = 0;
    fd_set rSet;
    struct timeval timeout;

    _captureCritSect->Enter();

    FD_ZERO(&rSet);
    FD_SET(_deviceFd, &rSet);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    retVal = select(_deviceFd + 1, &rSet, NULL, NULL, &timeout);
    if (retVal < 0 && errno != EINTR) // continue if interrupted
    {
        // select failed
        _captureCritSect->Leave();
        return false;
    }
    else if (retVal == 0)
    {
        // select timed out
        _captureCritSect->Leave();
        return true;
    }
    else if (!FD_ISSET(_deviceFd, &rSet))
    {
        // not event on camera handle
        _captureCritSect->Leave();
        return true;
    }

    if (_captureStarted)
    {
        char buff[64*1024] = {0};
        int received = ::recv(_deviceFd, buff, 64*1024, 0);
        LOG(INFO) << "received:" << received;
        
        VideoCaptureCapability frameInfo;
        frameInfo.width = _currentWidth;
        frameInfo.height = _currentHeight;
        frameInfo.rawType = _captureVideoType;

        IncomingFrame((uint8_t*)buff, received, frameInfo, 0);
    }
    _captureCritSect->Leave();
    usleep(0);
    return true;
}
    

int32_t NetCapturer::IncomingFrame(
    uint8_t* videoFrame,
    size_t videoFrameLength,
    const VideoCaptureCapability& frameInfo,
    int64_t captureTime/*=0*/)
{
    CriticalSectionScoped cs(_apiCs);

    const int32_t width = frameInfo.width;
    const int32_t height = frameInfo.height;

    int stride_y = width;
    int stride_uv = (width + 1) / 2;
    int target_width = width;
    int target_height = height;

    rtc::scoped_refptr<I420Buffer> buffer = I420Buffer::Create(
        target_width, abs(target_height), stride_y, stride_uv, stride_uv);
    webrtc::I420Buffer::SetBlack(buffer.get());

    rtc::scoped_refptr<webrtc::VideoFrameAttachment> attachment =
        new rtc::RefCountedObject<webrtc::VideoFrameAttachment>(videoFrame, videoFrameLength);
    
    VideoFrame captureFrame(
        buffer, attachment, 0, rtc::TimeMillis(),
        kVideoRotation_0);
    captureFrame.set_ntp_time_ms(captureTime);
    
    OnFrame(captureFrame, width, height);
    return 0;
}


    
bool NetCapturer::IsRunning() {
    return _captureStarted;
}


}  // namespace webrtc
