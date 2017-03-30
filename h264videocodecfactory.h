/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */
#ifndef WEBRTC_EXAMPLES_VOIP_H264_VIDEO_CODEC_FACTORY_H_
#define WEBRTC_EXAMPLES_VOIP_H264_VIDEO_CODEC_FACTORY_H_

#include "webrtc/media/engine/webrtcvideoencoderfactory.h"

namespace webrtc {

class H264VideoEncoderFactory
    : public cricket::WebRtcVideoEncoderFactory {
 public:
  H264VideoEncoderFactory();
  ~H264VideoEncoderFactory();

  // WebRtcVideoEncoderFactory implementation.
  VideoEncoder* CreateVideoEncoder(const cricket::VideoCodec& codec) override;
  void DestroyVideoEncoder(VideoEncoder* encoder) override;
  const std::vector<cricket::VideoCodec>& supported_codecs() const override;

 private:
  // TODO(magjed): Mutable because it depends on a field trial and it is
  // recalculated every call to supported_codecs().
  mutable std::vector<cricket::VideoCodec> supported_codecs_;
};

}  // namespace webrtc

#endif  // WEBRTC_SDK_OBJC_FRAMEWORK_CLASSES_VIDEOTOOLBOXVIDEOCODECFACTORY_H_
