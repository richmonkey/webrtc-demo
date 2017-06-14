/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_EXAMPLES_IM_CLIENT_VIDEO_RENDERER_H_
#define WEBRTC_EXAMPLES_IM_CLIENT_VIDEO_RENDERER_H_

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


class VideoRenderer : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
    VideoRenderer(HWND wnd, int width, int height,
                  webrtc::VideoTrackInterface* track_to_render);
    virtual ~VideoRenderer();

    void Lock() {
        ::EnterCriticalSection(&buffer_lock_);
    }

    void Unlock() {
        ::LeaveCriticalSection(&buffer_lock_);
    }

    // VideoSinkInterface implementation
    void OnFrame(const webrtc::VideoFrame& frame) override;

    const BITMAPINFO& bmi() const { return bmi_; }
    const uint8_t* image() const { return image_.get(); }

 protected:
    void SetSize(int width, int height);

    enum {
        SET_SIZE,
        RENDER_FRAME,
    };

    HWND wnd_;
    BITMAPINFO bmi_;
    std::unique_ptr<uint8_t[]> image_;
    CRITICAL_SECTION buffer_lock_;
    rtc::scoped_refptr<webrtc::VideoTrackInterface> rendered_track_;
};

#endif
