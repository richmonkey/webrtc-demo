/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/examples/voip/linux/videocapturer.h"

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


VideoCaptureV4L2::VideoCaptureV4L2() :
    _captureCritSect(CriticalSectionWrapper::CreateCriticalSection()),
    _apiCs(CriticalSectionWrapper::CreateCriticalSection()),
    _deviceId(0),
    _deviceFd(-1),
    _buffersAllocatedByDevice(-1),
    _currentWidth(-1),
    _currentHeight(-1),
    _currentFrameRate(-1),
    _captureStarted(false),
    _captureVideoType(kVideoI420),
    _pool(NULL) {

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
}

VideoCaptureV4L2::~VideoCaptureV4L2() {

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

    
std::set<cricket::VideoFormat> VideoCaptureV4L2::FillCapabilities(int fd)
{

    std::set<cricket::VideoFormat> supportedFormats;
    // set image format
    struct v4l2_format video_fmt;
    memset(&video_fmt, 0, sizeof(struct v4l2_format));

    video_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video_fmt.fmt.pix.sizeimage = 0;

    int totalFmts = 4;
    unsigned int videoFormats[] = {
        V4L2_PIX_FMT_MJPEG,
        V4L2_PIX_FMT_YUV420,
        V4L2_PIX_FMT_YUYV,
        V4L2_PIX_FMT_UYVY };

    int sizes = 13;
    unsigned int size[][2] = { { 128, 96 }, { 160, 120 }, { 176, 144 },
                               { 320, 240 }, { 352, 288 }, { 640, 480 },
                               { 704, 576 }, { 800, 600 }, { 960, 720 },
                               { 1280, 720 }, { 1024, 768 }, { 1440, 1080 },
                               { 1920, 1080 } };

    int index = 0;
    for (int fmts = 0; fmts < totalFmts; fmts++)
    {
        for (int i = 0; i < sizes; i++)
        {
            video_fmt.fmt.pix.pixelformat = videoFormats[fmts];
            video_fmt.fmt.pix.width = size[i][0];
            video_fmt.fmt.pix.height = size[i][1];

            if (ioctl(fd, VIDIOC_TRY_FMT, &video_fmt) >= 0)
            {
                if ((video_fmt.fmt.pix.width == size[i][0])
                    && (video_fmt.fmt.pix.height == size[i][1]))
                {
                    cricket::VideoFormat format;
                    format.width = video_fmt.fmt.pix.width;
                    format.height = video_fmt.fmt.pix.height;
                    if (videoFormats[fmts] == V4L2_PIX_FMT_YUYV)
                    {
                        format.fourcc = cricket::FOURCC_YUY2;
                    }
                    else if (videoFormats[fmts] == V4L2_PIX_FMT_YUV420)
                    {
                        format.fourcc = cricket::FOURCC_I420;
                    }
                    else if (videoFormats[fmts] == V4L2_PIX_FMT_MJPEG)
                    {
                        format.fourcc = cricket::FOURCC_MJPG;
                    }
                    else if (videoFormats[fmts] == V4L2_PIX_FMT_UYVY)
                    {
                        format.fourcc = cricket::FOURCC_UYVY;
                    }

                    // get fps of current camera mode
                    // V4l2 does not have a stable method of knowing so we just guess.
                    if(format.width >= 800 && format.fourcc != cricket::FOURCC_MJPG)
                    {
                        format.interval = format.FpsToInterval(15);
                    }
                    else
                    {
                        format.interval = format.FpsToInterval(30);
                    }

                    supportedFormats.insert(format);
                    
                    index++;
                    WEBRTC_TRACE(
                        webrtc::kTraceInfo, webrtc::kTraceVideoCapture, 0,
                        "Camera format, width:%d height:%d fourcc:%d interval:%d",
                        format.width, format.height, format.fourcc, format.interval);
                }
            }
        }
    }

    return supportedFormats;
}



cricket::CaptureState VideoCaptureV4L2::Start(const cricket::VideoFormat& format) 
{
    if (_captureStarted)
    {
          LOG(LS_ERROR) << "The capturer is already running.";
          return cricket::CaptureState::CS_FAILED;
    }

    CriticalSectionScoped cs(_captureCritSect);
    //first open /dev/video device
    char device[20];
    sprintf(device, "/dev/video%d", (int) _deviceId);

    if ((_deviceFd = open(device, O_RDWR | O_NONBLOCK, 0)) < 0)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                   "error in opening %s errono = %d", device, errno);
        return cricket::CaptureState::CS_FAILED;

    }

    // Supported video formats in preferred order.
    // If the requested resolution is larger than VGA, we prefer MJPEG. Go for
    // I420 otherwise.
    const int nFormats = 5;
    unsigned int fmts[nFormats];
    if (format.width > 640 || format.height > 480) {
        fmts[0] = V4L2_PIX_FMT_MJPEG;
        fmts[1] = V4L2_PIX_FMT_YUV420;
        fmts[2] = V4L2_PIX_FMT_YUYV;
        fmts[3] = V4L2_PIX_FMT_UYVY;
        fmts[4] = V4L2_PIX_FMT_JPEG;
    } else {
        fmts[0] = V4L2_PIX_FMT_YUV420;
        fmts[1] = V4L2_PIX_FMT_YUYV;
        fmts[2] = V4L2_PIX_FMT_UYVY;
        fmts[3] = V4L2_PIX_FMT_MJPEG;
        fmts[4] = V4L2_PIX_FMT_JPEG;
    }

    // Enumerate image formats.
    struct v4l2_fmtdesc fmt;
    int fmtsIdx = nFormats;
    memset(&fmt, 0, sizeof(fmt));
    fmt.index = 0;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, 0,
                 "Video Capture enumerats supported image formats:");
    while (ioctl(_deviceFd, VIDIOC_ENUM_FMT, &fmt) == 0) {
        WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, 0,
                     "  { pixelformat = %c%c%c%c, description = '%s' }",
                     fmt.pixelformat & 0xFF, (fmt.pixelformat>>8) & 0xFF,
                     (fmt.pixelformat>>16) & 0xFF, (fmt.pixelformat>>24) & 0xFF,
                     fmt.description);
        // Match the preferred order.
        for (int i = 0; i < nFormats; i++) {
            if (fmt.pixelformat == fmts[i] && i < fmtsIdx)
                fmtsIdx = i;
        }
        // Keep enumerating.
        fmt.index++;
    }

    if (fmtsIdx == nFormats)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                     "no supporting video formats found");
        return cricket::CaptureState::CS_FAILED;
    } else {
        WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, 0,
                     "We prefer format %c%c%c%c",
                     fmts[fmtsIdx] & 0xFF, (fmts[fmtsIdx]>>8) & 0xFF,
                     (fmts[fmtsIdx]>>16) & 0xFF, (fmts[fmtsIdx]>>24) & 0xFF);
    }

    struct v4l2_format video_fmt;
    memset(&video_fmt, 0, sizeof(struct v4l2_format));
    video_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video_fmt.fmt.pix.sizeimage = 0;
    video_fmt.fmt.pix.width = format.width;
    video_fmt.fmt.pix.height = format.height;
    video_fmt.fmt.pix.pixelformat = fmts[fmtsIdx];

    if (video_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
        _captureVideoType = kVideoYUY2;
    else if (video_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420)
        _captureVideoType = kVideoI420;
    else if (video_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_UYVY)
        _captureVideoType = kVideoUYVY;
    else if (video_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG ||
             video_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_JPEG)
        _captureVideoType = kVideoMJPEG;

    //set format and frame size now
    if (ioctl(_deviceFd, VIDIOC_S_FMT, &video_fmt) < 0)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                   "error in VIDIOC_S_FMT, errno = %d", errno);
        return cricket::CaptureState::CS_FAILED;        
    }

    // initialize current width and height
    _currentWidth = video_fmt.fmt.pix.width;
    _currentHeight = video_fmt.fmt.pix.height;

    // Trying to set frame rate, before check driver capability.
    bool driver_framerate_support = true;
    struct v4l2_streamparm streamparms;
    memset(&streamparms, 0, sizeof(streamparms));
    streamparms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(_deviceFd, VIDIOC_G_PARM, &streamparms) < 0) {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                   "error in VIDIOC_G_PARM errno = %d", errno);
        driver_framerate_support = false;
      // continue
    } else {
      // check the capability flag is set to V4L2_CAP_TIMEPERFRAME.
      if (streamparms.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
        // driver supports the feature. Set required framerate.
        memset(&streamparms, 0, sizeof(streamparms));
        streamparms.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        streamparms.parm.capture.timeperframe.numerator = 1;
        streamparms.parm.capture.timeperframe.denominator = format.framerate();
        if (ioctl(_deviceFd, VIDIOC_S_PARM, &streamparms) < 0) {
          WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                   "Failed to set the framerate. errno=%d", errno);
          driver_framerate_support = false;
        } else {
          _currentFrameRate = format.framerate();
        }
      }
    }
    // If driver doesn't support framerate control, need to hardcode.
    // Hardcoding the value based on the frame size.
    if (!driver_framerate_support) {
      if(_currentWidth >= 800 && _captureVideoType != kVideoMJPEG) {
        _currentFrameRate = 15;
      } else {
        _currentFrameRate = 30;
      }
    }

    if (!AllocateVideoBuffers())
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                   "failed to allocate video capture buffers");
        return cricket::CaptureState::CS_FAILED;        
    }

    //start capture thread;
    if (!_captureThread)
    {
        _captureThread.reset(new rtc::PlatformThread(
            VideoCaptureV4L2::CaptureThread, this, "CaptureThread"));
        _captureThread->Start();
        _captureThread->SetPriority(rtc::kHighPriority);
    }

    // Needed to start UVC camera - from the uvcview application
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(_deviceFd, VIDIOC_STREAMON, &type) == -1)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                     "Failed to turn on stream");
        return cricket::CaptureState::CS_FAILED;                
    }

    SetCaptureFormat(&format);
    
    _captureStarted = true;

    SetCaptureState(cricket::CaptureState::CS_RUNNING);

    return cricket::CaptureState::CS_STARTING;
}

void VideoCaptureV4L2::Stop()
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

        DeAllocateVideoBuffers();
        close(_deviceFd);
        _deviceFd = -1;
    }

    SetCaptureFormat(NULL);
}

    
//critical section protected by the caller

bool VideoCaptureV4L2::AllocateVideoBuffers()
{
    struct v4l2_requestbuffers rbuffer;
    memset(&rbuffer, 0, sizeof(v4l2_requestbuffers));

    rbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rbuffer.memory = V4L2_MEMORY_MMAP;
    rbuffer.count = kNoOfV4L2Bufffers;

    if (ioctl(_deviceFd, VIDIOC_REQBUFS, &rbuffer) < 0)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                   "Could not get buffers from device. errno = %d", errno);
        return false;
    }

    if (rbuffer.count > kNoOfV4L2Bufffers)
        rbuffer.count = kNoOfV4L2Bufffers;

    _buffersAllocatedByDevice = rbuffer.count;

    //Map the buffers
    _pool = new Buffer[rbuffer.count];

    for (unsigned int i = 0; i < rbuffer.count; i++)
    {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(v4l2_buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (ioctl(_deviceFd, VIDIOC_QUERYBUF, &buffer) < 0)
        {
            return false;
        }

        _pool[i].start = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                              _deviceFd, buffer.m.offset);

        if (MAP_FAILED == _pool[i].start)
        {
            for (unsigned int j = 0; j < i; j++)
                munmap(_pool[j].start, _pool[j].length);
            return false;
        }

        _pool[i].length = buffer.length;

        if (ioctl(_deviceFd, VIDIOC_QBUF, &buffer) < 0)
        {
            return false;
        }
    }
    return true;
}

bool VideoCaptureV4L2::DeAllocateVideoBuffers()
{
    // unmap buffers
    for (int i = 0; i < _buffersAllocatedByDevice; i++)
        munmap(_pool[i].start, _pool[i].length);

    delete[] _pool;

    // turn off stream
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(_deviceFd, VIDIOC_STREAMOFF, &type) < 0)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                   "VIDIOC_STREAMOFF error. errno: %d", errno);
    }

    return true;
}


bool VideoCaptureV4L2::CaptureThread(void* obj)
{
    return static_cast<VideoCaptureV4L2*> (obj)->CaptureProcess();
}
    
bool VideoCaptureV4L2::CaptureProcess()
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
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        // dequeue a buffer - repeat until dequeued properly!
        while (ioctl(_deviceFd, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno != EINTR)
            {
                WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, 0,
                           "could not sync on a buffer on device %s", strerror(errno));
                _captureCritSect->Leave();
                return true;
            }
        }
        VideoCaptureCapability frameInfo;
        frameInfo.width = _currentWidth;
        frameInfo.height = _currentHeight;
        frameInfo.rawType = _captureVideoType;

        // convert to to I420 if needed
        IncomingFrame((unsigned char*) _pool[buf.index].start,
                      buf.bytesused, frameInfo, 0);
        // enqueue the buffer again
        if (ioctl(_deviceFd, VIDIOC_QBUF, &buf) == -1)
        {
            WEBRTC_TRACE(webrtc::kTraceWarning, webrtc::kTraceVideoCapture, 0,
                       "Failed to enqueue capture buffer");
        }
    }
    _captureCritSect->Leave();
    usleep(0);
    return true;
}
    

int32_t VideoCaptureV4L2::IncomingFrame(
    uint8_t* videoFrame,
    size_t videoFrameLength,
    const VideoCaptureCapability& frameInfo,
    int64_t captureTime/*=0*/)
{
    CriticalSectionScoped cs(_apiCs);

    const int32_t width = frameInfo.width;
    const int32_t height = frameInfo.height;


    // Not encoded, convert to I420.
    const VideoType commonVideoType =
              RawVideoTypeToCommonVideoVideoType(frameInfo.rawType);

    if (frameInfo.rawType != kVideoMJPEG &&
        CalcBufferSize(commonVideoType, width,
                       abs(height)) != videoFrameLength)
    {
        LOG(LS_ERROR) << "Wrong incoming frame length.";
        return -1;
    }

    int stride_y = width;
    int stride_uv = (width + 1) / 2;
    int target_width = width;
    int target_height = height;


    // Setting absolute height (in case it was negative).
    // In Windows, the image starts bottom left, instead of top left.
    // Setting a negative source height, inverts the image (within LibYuv).

    // TODO(nisse): Use a pool?
    rtc::scoped_refptr<I420Buffer> buffer = I420Buffer::Create(
        target_width, abs(target_height), stride_y, stride_uv, stride_uv);
    const int conversionResult = ConvertToI420(
        commonVideoType, videoFrame, 0, 0,  // No cropping
        width, height, videoFrameLength,
         kVideoRotation_0, buffer.get());
    if (conversionResult < 0)
    {
      LOG(LS_ERROR) << "Failed to convert capture frame from type "
                    << frameInfo.rawType << "to I420.";
        return -1;
    }

    VideoFrame captureFrame(
        buffer, 0, rtc::TimeMillis(),
        kVideoRotation_0);
    captureFrame.set_ntp_time_ms(captureTime);

    //disable adapt
    
    OnFrame(captureFrame, width, height);
     
    return 0;
}


    
bool VideoCaptureV4L2::IsRunning() {
    return _captureStarted;
}


}  // namespace webrtc
