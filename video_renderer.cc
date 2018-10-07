#include "webrtc/examples/voip/video_renderer.h"
#include <math.h>

#include "libyuv/convert_argb.h"
#include "webrtc/api/video/i420_buffer.h"
#include "webrtc/base/arraysize.h"
#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"

// A little helper class to make sure we always to proper locking and
// unlocking when working with VideoRenderer buffers.
template <typename T>
class AutoLock {
public:
    explicit AutoLock(T* obj) : obj_(obj) { obj_->Lock(); }
    ~AutoLock() { obj_->Unlock(); }
protected:
    T* obj_;
};


VideoRenderer::VideoRenderer(HWND wnd, int width, int height,
                             webrtc::VideoTrackInterface* track_to_render)
    : wnd_(wnd), rendered_track_(track_to_render) {
    ::InitializeCriticalSection(&buffer_lock_);
    ZeroMemory(&bmi_, sizeof(bmi_));
    bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi_.bmiHeader.biPlanes = 1;
    bmi_.bmiHeader.biBitCount = 32;
    bmi_.bmiHeader.biCompression = BI_RGB;
    bmi_.bmiHeader.biWidth = width;
    bmi_.bmiHeader.biHeight = -height;
    bmi_.bmiHeader.biSizeImage = width * height *
        (bmi_.bmiHeader.biBitCount >> 3);
    rendered_track_->AddOrUpdateSink(this, rtc::VideoSinkWants());
}

VideoRenderer::~VideoRenderer() {
    rendered_track_->RemoveSink(this);
    ::DeleteCriticalSection(&buffer_lock_);
}

void VideoRenderer::SetSize(int width, int height) {
    AutoLock<VideoRenderer> lock(this);

    if (width == bmi_.bmiHeader.biWidth && height == bmi_.bmiHeader.biHeight) {
        return;
    }

    bmi_.bmiHeader.biWidth = width;
    bmi_.bmiHeader.biHeight = -height;
    bmi_.bmiHeader.biSizeImage = width * height *
        (bmi_.bmiHeader.biBitCount >> 3);
    image_.reset(new uint8_t[bmi_.bmiHeader.biSizeImage]);
}

void VideoRenderer::OnFrame(const webrtc::VideoFrame& video_frame) {
    {
        AutoLock<VideoRenderer> lock(this);

        rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer(
                                                            video_frame.video_frame_buffer());
        if (video_frame.rotation() != webrtc::kVideoRotation_0) {
            buffer = webrtc::I420Buffer::Rotate(*buffer, video_frame.rotation());
        }

        SetSize(buffer->width(), buffer->height());

        RTC_DCHECK(image_.get() != NULL);
        libyuv::I420ToARGB(buffer->DataY(), buffer->StrideY(),
                           buffer->DataU(), buffer->StrideU(),
                           buffer->DataV(), buffer->StrideV(),
                           image_.get(),
                           bmi_.bmiHeader.biWidth *
                           bmi_.bmiHeader.biBitCount / 8,
                           buffer->width(), buffer->height());
    }
    InvalidateRect(wnd_, NULL, TRUE);
}
