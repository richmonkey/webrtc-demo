/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#include "webrtc/examples/voip/net_encoder.h"

#include <limits>
#include <string>
#include <arpa/inet.h>

#include "third_party/openh264/src/codec/api/svc/codec_api.h"
#include "third_party/openh264/src/codec/api/svc/codec_app_def.h"
#include "third_party/openh264/src/codec/api/svc/codec_def.h"
#include "third_party/openh264/src/codec/api/svc/codec_ver.h"

#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "webrtc/media/base/mediaconstants.h"
#include "webrtc/system_wrappers/include/metrics.h"

namespace webrtc {

NetEncoder::NetEncoder(const cricket::VideoCodec& codec)
    : packetization_mode_(H264PacketizationMode::NonInterleaved) {
  RTC_CHECK(cricket::CodecNamesEq(codec.name, cricket::kH264CodecName));
  std::string packetization_mode_string;
  if (codec.GetParam(cricket::kH264FmtpPacketizationMode,
                     &packetization_mode_string) &&
      packetization_mode_string == "1") {
    packetization_mode_ = H264PacketizationMode::NonInterleaved;
  }
}

NetEncoder::~NetEncoder() {
  Release();
}

int32_t NetEncoder::InitEncode(const VideoCodec* codec_settings,
                                    int32_t number_of_cores,
                                    size_t max_payload_size) {

    // Initialize encoded image. Default buffer size: size of unencoded data.
  encoded_image_._size =
      CalcBufferSize(kI420, codec_settings->width, codec_settings->height);
  encoded_image_._buffer = new uint8_t[encoded_image_._size];
  encoded_image_buffer_.reset(encoded_image_._buffer);
  encoded_image_._completeFrame = true;
  encoded_image_._encodedWidth = 0;
  encoded_image_._encodedHeight = 0;
  encoded_image_._length = 0;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetEncoder::Release() {

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetEncoder::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  encoded_image_callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetEncoder::SetRateAllocation(
    const BitrateAllocation& bitrate_allocation,
    uint32_t framerate) {

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetEncoder::Encode(const VideoFrame& input_frame,
                                const CodecSpecificInfo* codec_specific_info,
                                const std::vector<FrameType>* frame_types) {
    rtc::scoped_refptr<webrtc::VideoFrameAttachment> attachment = input_frame.video_frame_attachment();
    LOG(INFO) << "encode...:" << attachment->length_;

    uint8_t *p = attachment->data_;
    size_t length = attachment->length_;

    if (length < 4) {
        LOG(INFO) << "attachment invalid length:" << length;
        return WEBRTC_VIDEO_CODEC_OK;
    }
    
    uint16_t frameType;
    memcpy(&frameType, p, 2);
    frameType = ntohs(frameType);
    p += 2;
    
    uint16_t fragmentationVectorSize;
    memcpy(&fragmentationVectorSize, p, 2);
    fragmentationVectorSize = ntohs(fragmentationVectorSize);
    p += 2;
    if (2 + 2 + fragmentationVectorSize*2*4 > length) {
        LOG(INFO) << "attachment invalid fragment size:" << fragmentationVectorSize;
        return WEBRTC_VIDEO_CODEC_OK;
    }

    rtc::scoped_refptr<const VideoFrameBuffer> frame_buffer =
        input_frame.video_frame_buffer();
    encoded_image_._encodedWidth = frame_buffer->width();
    encoded_image_._encodedHeight = frame_buffer->height();
    encoded_image_._timeStamp = input_frame.timestamp();
    encoded_image_.ntp_time_ms_ = input_frame.ntp_time_ms();
    encoded_image_.capture_time_ms_ = input_frame.render_time_ms();
    encoded_image_.rotation_ = input_frame.rotation();
    encoded_image_._frameType = (webrtc::FrameType)frameType;
    RTPFragmentationHeader frag_header;
    frag_header.VerifyAndAllocateFragmentationHeader(fragmentationVectorSize);
    for (int i = 0; i < fragmentationVectorSize; i++) {
        int32_t offset;
        int32_t len;

        memcpy(&offset, p, 4);
        offset = ntohl(offset);
        p += 4;

        memcpy(&len, p, 4);
        len = ntohl(len);
        p += 4;

        frag_header.fragmentationOffset[i] = offset;
        frag_header.fragmentationLength[i] = len;
    }

    
    int encodedLength = length - 2 - 2 - fragmentationVectorSize*2*4;
    if (encoded_image_._size < (size_t)encodedLength) {
        encoded_image_._size =
            CalcBufferSize(kI420, frame_buffer->width(), frame_buffer->height());

        if (encoded_image_._size < (size_t)encodedLength) {
            encoded_image_._size = encodedLength;
        }

        encoded_image_._buffer = new uint8_t[encoded_image_._size];
        encoded_image_buffer_.reset(encoded_image_._buffer);
    }
    
    memcpy(encoded_image_._buffer, p, encodedLength);
    encoded_image_._length = encodedLength;


    if (encoded_image_._length > 0) {
        // Deliver encoded image.
        LOG(INFO) << "encoded image width:" << encoded_image_._encodedWidth
                  << " height:" << encoded_image_._encodedHeight
                  << " length:" << encoded_image_._length;
        
        CodecSpecificInfo codec_specific;
        codec_specific.codecType = kVideoCodecH264;
        codec_specific.codecSpecific.H264.packetization_mode = packetization_mode_;
        encoded_image_callback_->OnEncodedImage(encoded_image_, &codec_specific,
                                                &frag_header);
    }
    
    return WEBRTC_VIDEO_CODEC_OK;
}

const char* NetEncoder::ImplementationName() const {
  return "OpenH264";
}



int32_t NetEncoder::SetChannelParameters(
    uint32_t packet_loss, int64_t rtt) {
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NetEncoder::SetPeriodicKeyFrames(bool enable) {
  return WEBRTC_VIDEO_CODEC_OK;
}

VideoEncoder::ScalingSettings NetEncoder::GetScalingSettings() const {
  return VideoEncoder::ScalingSettings(true);
}

}  // namespace webrtc
