/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "webrtc/examples/voip/h264videocodecfactory.h"
#include "webrtc/examples/voip/net_encoder.h"

#include "webrtc/modules/video_coding/codecs/h264/include/h264.h"


#include "webrtc/base/logging.h"
#include "webrtc/common_video/h264/profile_level_id.h"
#include "webrtc/media/base/codec.h"
#include "webrtc/system_wrappers/include/field_trial.h"

namespace webrtc {

H264VideoEncoderFactory::H264VideoEncoderFactory() {
}

H264VideoEncoderFactory::~H264VideoEncoderFactory() {}

VideoEncoder* H264VideoEncoderFactory::CreateVideoEncoder(
    const cricket::VideoCodec& codec) {
    
    const webrtc::VideoCodecType codec_type =
        webrtc::PayloadNameToCodecType(codec.name)
        .value_or(webrtc::kVideoCodecUnknown);
    switch (codec_type) {
    case webrtc::kVideoCodecH264: {
        LOG(INFO) << "create encoder.............";
        VideoEncoder *encoder = new NetEncoder(codec);
        //VideoEncoder *encoder = webrtc::H264Encoder::Create(codec);
        return encoder;
    }
    default:
        return nullptr;
    }
}
    

void H264VideoEncoderFactory::DestroyVideoEncoder(
    VideoEncoder* encoder) {
  delete encoder;
  encoder = nullptr;
}

const std::vector<cricket::VideoCodec>&
H264VideoEncoderFactory::supported_codecs() const {
  supported_codecs_.clear();


  if (webrtc::H264Encoder::IsSupported()) {
      cricket::VideoCodec codec(cricket::kH264CodecName);
    // TODO(magjed): Move setting these parameters into webrtc::H264Encoder
    // instead.
    codec.SetParam(cricket::kH264FmtpProfileLevelId,
                   cricket::kH264ProfileLevelConstrainedBaseline);
    codec.SetParam(cricket::kH264FmtpLevelAsymmetryAllowed, "1");
    codec.SetParam(cricket::kH264FmtpPacketizationMode, "1");    
    supported_codecs_.push_back(std::move(codec));
  }

    
  return supported_codecs_;
}


}  // namespace webrtc
