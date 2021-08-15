/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/voip/conductor.h"

#include <memory>
#include <utility>
#include <vector>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/create_peerconnection_factory.h"

#include "rtc_base/checks.h"
#include "rtc_base/strings/json.h"
#include "rtc_base/logging.h"
#include "pc/video_track_source.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_factory.h"
#include "test/vcm_capturer.h"
#include "examples/voip/defaults.h"


// Names used for a IceCandidate JSON object.
const char kCandidateSdpMidName[] = "id";//"sdpMid";
const char kCandidateSdpMlineIndexName[] = "label";//"sdpMLineIndex";
const char kCandidateSdpName[] = "candidate";

// Names used for a SessionDescription JSON object.
const char kSessionDescriptionTypeName[] = "type";
const char kSessionDescriptionSdpName[] = "sdp";

#define DTLS_ON  true
#define DTLS_OFF false

class MessageData: public rtc::MessageData {
public:
    MessageData(void* data):data_(data) {}
    void *data_;
};


class DummySetSessionDescriptionObserver
    : public webrtc::SetSessionDescriptionObserver {
 public:
  static DummySetSessionDescriptionObserver* Create() {
    return
        new rtc::RefCountedObject<DummySetSessionDescriptionObserver>();
  }
  virtual void OnSuccess() {
    RTC_LOG(INFO) << __FUNCTION__;
  }


  virtual void OnFailure(webrtc::RTCError error) {
    RTC_LOG(LERROR) << ToString(error.type()) << ": " << error.message();
  }    

 protected:
  DummySetSessionDescriptionObserver() {}
  ~DummySetSessionDescriptionObserver() {}
};


class DummyAudioTrackSink : public webrtc::AudioTrackSinkInterface {
public:
    DummyAudioTrackSink(const std::string& name): name_(name) {}
    virtual void OnData(const void* audio_data,
                        int bits_per_sample,
                        int sample_rate,
                        size_t number_of_channels,
                        size_t number_of_frames) {
        RTC_LOG(INFO) << "audio track: " << name_ << " number of frames:" << number_of_frames;
    }

private:
    const std::string name_;
    
};


class CapturerTrackSource : public webrtc::VideoTrackSource {
 public:
  static rtc::scoped_refptr<CapturerTrackSource> Create() {
    const size_t kWidth = 640;
    const size_t kHeight = 480;
    const size_t kFps = 30;
    std::unique_ptr<webrtc::test::VcmCapturer> capturer;
    std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
        webrtc::VideoCaptureFactory::CreateDeviceInfo());
    if (!info) {
      return nullptr;
    }
    int num_devices = info->NumberOfDevices();
    for (int i = 0; i < num_devices; ++i) {
      capturer = absl::WrapUnique(
          webrtc::test::VcmCapturer::Create(kWidth, kHeight, kFps, i));
      if (capturer) {
        return new rtc::RefCountedObject<CapturerTrackSource>(
            std::move(capturer));
      }
    }

    return nullptr;
  }

 protected:
  explicit CapturerTrackSource(
      std::unique_ptr<webrtc::test::VcmCapturer> capturer)
      : VideoTrackSource(/*remote=*/false), capturer_(std::move(capturer)) {}

 private:
  rtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return capturer_.get();
  }
  std::unique_ptr<webrtc::test::VcmCapturer> capturer_;
};


Conductor::Conductor(PeerConnectionClient* client,
                     rtc::Thread* main_thread,
                     int64_t uid,
                     std::string& token)
  : peer_id_(-1),
    loopback_(false),
    client_(client),
    main_thread_(main_thread),
    uid_(uid),
    token_(token) {
    
    _networkThread = rtc::Thread::CreateWithSocketServer();
    bool result = _networkThread->Start();
    if (!result) {
        RTC_LOG(INFO) <<"start thread error";
    }
    
    _workerThread = rtc::Thread::Create();
    result = _workerThread->Start();


    _signalingThread = rtc::Thread::Create();
    result = _signalingThread->Start();


}

Conductor::~Conductor() {
  RTC_DCHECK(peer_connection_.get() == NULL);
}

bool Conductor::connection_active() const {
  return peer_connection_.get() != NULL;
}

void Conductor::Close() {
  DeletePeerConnection();
}

bool Conductor::InitializePeerConnection() {
  RTC_DCHECK(peer_connection_factory_.get() == NULL);
  RTC_DCHECK(peer_connection_.get() == NULL);

//    peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
//      nullptr /* network_thread */, nullptr /* worker_thread */,
//      nullptr /* signaling_thread */, nullptr /* default_adm */,
//      webrtc::CreateBuiltinAudioEncoderFactory(),
//      webrtc::CreateBuiltinAudioDecoderFactory(),
//      webrtc::CreateBuiltinVideoEncoderFactory(),
//      webrtc::CreateBuiltinVideoDecoderFactory(), nullptr /* audio_mixer */,
//      nullptr /* audio_processing */);


  peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
      _networkThread.get(),
      _workerThread.get(),
      _signalingThread.get(),
      nullptr,
      webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory(),
      webrtc::CreateBuiltinVideoEncoderFactory(),
      webrtc::CreateBuiltinVideoDecoderFactory(),
      nullptr, nullptr);

  
  //peer_connection_factory_  = webrtc::CreatePeerConnectionFactory();

  if (!peer_connection_factory_.get()) {
    RTC_LOG(WARNING) << "Failed to initialize PeerConnectionFactory";
    DeletePeerConnection();
    return false;
  }

  RTC_LOG(INFO) << "CreatePeerConnection...";  
  if (!CreatePeerConnection(DTLS_ON)) {
    RTC_LOG(WARNING) << "CreatePeerConnection failed";
    DeletePeerConnection();
    return false;
  }
  AddTracks();
  return peer_connection_.get() != NULL;
}


bool Conductor::CreatePeerConnection(bool dtls) {
  RTC_DCHECK(peer_connection_factory_.get() != NULL);
  RTC_DCHECK(peer_connection_.get() == NULL);

  webrtc::PeerConnectionInterface::RTCConfiguration config;
  webrtc::PeerConnectionInterface::IceServer server;
  server.uri = "stun:stun.counterpath.net:3478";
  config.servers.push_back(server);
  webrtc::PeerConnectionInterface::IceServer server2;
  server2.uri = "turn:turn.gobelieve.io:3478?transport=udp";
  char s[64] = {0};
  snprintf(s, 64, "7_%ld", uid_);
  server2.username = s;
  server2.password = token_;
  config.servers.push_back(server2);
  
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;  
  config.enable_dtls_srtp = dtls;


  webrtc::PeerConnectionDependencies dependencies(this);  
  peer_connection_ = peer_connection_factory_->CreatePeerConnection(config, std::move(dependencies));
  return peer_connection_ != NULL;
}

void Conductor::DeletePeerConnection() {
  peer_connection_ = NULL;
  StopLocalRenderer();
  StopRemoteRenderer();  
  peer_connection_factory_ = NULL;
  peer_id_ = -1;
  loopback_ = false;
}

void Conductor::StartLocalRenderer(webrtc::VideoTrackInterface* local_video) {
  if (local_renderer_) {
    local_video_track_ = local_video;
    local_video->AddOrUpdateSink(local_renderer_, rtc::VideoSinkWants());
  }
}

void Conductor::StopLocalRenderer() {
  if (local_renderer_ && local_video_track_) {
    local_video_track_->RemoveSink(local_renderer_);
    local_video_track_ = NULL;
  }
}

void Conductor::StartRemoteRenderer(webrtc::VideoTrackInterface* remote_video) {
    if (remote_renderer_) {
        remote_video_track_ = remote_video;
        remote_video->AddOrUpdateSink(remote_renderer_, rtc::VideoSinkWants());
    }
}

void Conductor::StopRemoteRenderer() {
    if (remote_renderer_ && remote_video_track_) {
        remote_video_track_->RemoveSink(remote_renderer_);
        remote_video_track_ = NULL;
    }
}


//
// PeerConnectionObserver implementation.
//

// Called when a remote stream is added
void Conductor::OnAddStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  RTC_LOG(INFO) << __FUNCTION__ << " " << stream->id();

  this->AddRef();
  main_thread_->Post(RTC_FROM_HERE, this, NEW_STREAM_ADDED, new MessageData(stream.release()));
}

void Conductor::OnRemoveStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  RTC_LOG(INFO) << __FUNCTION__ << " " << stream->id();
  
  this->AddRef();
  main_thread_->Post(RTC_FROM_HERE, this, STREAM_REMOVED, new MessageData(stream.release()));
}

void Conductor::OnTrack(
    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
    RTC_LOG(INFO) << "ontrack...";
    rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track = transceiver->receiver()->track();
    if (track->kind() == "audio") {
        webrtc::AudioTrackInterface *audio_track = (webrtc::AudioTrackInterface*)track.get();
        //memoery leak
        auto sink = new DummyAudioTrackSink(std::string("remote"));        
        audio_track->AddSink(sink);
    }
}

void Conductor::OnAddTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {

    RTC_LOG(INFO) << "on add track 222";
}
    

void Conductor::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
  RTC_LOG(INFO) << __FUNCTION__ << " " << candidate->sdp_mline_index();
  // For loopback test. To save some connecting delay.
  if (loopback_) {
    if (!peer_connection_->AddIceCandidate(candidate)) {
      RTC_LOG(WARNING) << "Failed to apply the received candidate";
    }
    return;
  }

  Json::StyledWriter writer;
  Json::Value jmessage;
  jmessage["type"] = "candidate";
  jmessage[kCandidateSdpMidName] = candidate->sdp_mid();
  jmessage[kCandidateSdpMlineIndexName] = candidate->sdp_mline_index();
  std::string sdp;
  if (!candidate->ToString(&sdp)) {
    RTC_LOG(LS_ERROR) << "Failed to serialize candidate";
    return;
  }
  jmessage[kCandidateSdpName] = sdp;
  SendMessage(writer.write(jmessage));
}


void Conductor::OnPeerConnected(int id, const std::string& name) {
    RTC_LOG(INFO) << __FUNCTION__;
    if (!peer_connection_.get()) {
        RTC_DCHECK(peer_id_ == -1);
        peer_id_ = id;

        RTC_LOG(INFO) << "initialize peer connection";

        if (!InitializePeerConnection()) {
            RTC_LOG(LS_ERROR) << "Failed to initialize our PeerConnection instance";
            return;
        }
    }  
}

void Conductor::OnPeerDisconnected(int id) {
  RTC_LOG(INFO) << __FUNCTION__;
  if (id == peer_id_) {
    RTC_LOG(INFO) << "Our peer disconnected";
    DeletePeerConnection();
  }
}

void Conductor::OnMessageFromPeer(int peer_id, const std::string& message) {
  RTC_DCHECK(peer_id_ == peer_id || peer_id_ == -1);
  RTC_DCHECK(!message.empty());

  if (!peer_connection_.get()) {
    RTC_DCHECK(peer_id_ == -1);
    peer_id_ = peer_id;

    if (!InitializePeerConnection()) {
      RTC_LOG(LS_ERROR) << "Failed to initialize our PeerConnection instance";
      return;
    }
  } else if (peer_id != peer_id_) {
    RTC_DCHECK(peer_id_ != -1);
    RTC_LOG(WARNING) << "Received a message from unknown peer while already in a "
                    "conversation with a different peer.";
    return;
  }

  Json::Reader reader;
  Json::Value jmessage;
  if (!reader.parse(message, jmessage)) {
    RTC_LOG(WARNING) << "Received unknown message. " << message;
    return;
  }
  std::string type;
  std::string json_object;

  rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionTypeName, &type);
  if (type == "offer" || type == "answer" || type == "offer-loopback") {
    std::string sdp;
    if (!rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionSdpName,
                                      &sdp)) {
      RTC_LOG(WARNING) << "Can't parse received session description message.";
      return;
    }
    webrtc::SdpParseError error;
    webrtc::SessionDescriptionInterface* session_description(
        webrtc::CreateSessionDescription(type, sdp, &error));
    if (!session_description) {
      RTC_LOG(WARNING) << "Can't parse received session description message. "
          << "SdpParseError was: " << error.description;
      return;
    }
    RTC_LOG(INFO) << " Received session description :" << message;
    peer_connection_->SetRemoteDescription(
        DummySetSessionDescriptionObserver::Create(), session_description);
    if (session_description->type() ==
        webrtc::SessionDescriptionInterface::kOffer) {
      peer_connection_->CreateAnswer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    }
    return;
  } else if (type == "candidate") {
    std::string sdp_mid;
    int sdp_mlineindex = 0;
    std::string sdp;
    if (!rtc::GetStringFromJsonObject(jmessage, kCandidateSdpMidName,
                                      &sdp_mid) ||
        !rtc::GetIntFromJsonObject(jmessage, kCandidateSdpMlineIndexName,
                                   &sdp_mlineindex) ||
        !rtc::GetStringFromJsonObject(jmessage, kCandidateSdpName, &sdp)) {
      RTC_LOG(WARNING) << "Can't parse received message.";
      return;
    }
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> candidate(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));
    if (!candidate.get()) {
      RTC_LOG(WARNING) << "Can't parse received candidate message. "
          << "SdpParseError was: " << error.description;
      return;
    }
    if (!peer_connection_->AddIceCandidate(candidate.get())) {
      RTC_LOG(WARNING) << "Failed to apply the received candidate";
      return;
    }
    RTC_LOG(INFO) << " Received candidate :" << message;
    return;
  } else {
    RTC_LOG(WARNING) << "unknown type:" << type;
  }
}


void Conductor::OnServerConnectionFailure() {
    RTC_LOG(INFO) << "Failed to connect to server";
}

void Conductor::ConnectToPeer(int peer_id) {
    RTC_DCHECK(peer_id_ == -1);
    RTC_DCHECK(peer_id != -1);
    
    if (peer_connection_.get()) {
        RTC_LOG(INFO) << "We only support connecting to one peer at a time";
        return;
    }

    if (InitializePeerConnection()) {
        peer_id_ = peer_id;
        peer_connection_->CreateOffer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    } else {
        RTC_LOG(INFO) << "Failed to initialize PeerConnection";
    }
}


void Conductor::AddTracks() {
  if (!peer_connection_->GetSenders().empty()) {
    return;  // Already added tracks.
  }

  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
      peer_connection_factory_->CreateAudioTrack(
          kAudioLabel, peer_connection_factory_->CreateAudioSource(
                           cricket::AudioOptions())));
  auto result_or_error = peer_connection_->AddTrack(audio_track, {kStreamId});
  if (!result_or_error.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to add audio track to PeerConnection: "
                      << result_or_error.error().message();
  }

  //memoery leak
  auto sink = new DummyAudioTrackSink(std::string("local"));
  audio_track->AddSink(sink);

  rtc::scoped_refptr<CapturerTrackSource> video_device =
      CapturerTrackSource::Create();
  if (video_device) {
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
        peer_connection_factory_->CreateVideoTrack(kVideoLabel, video_device));
    StartLocalRenderer(video_track);

    result_or_error = peer_connection_->AddTrack(video_track, {kStreamId});
    if (!result_or_error.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to add video track to PeerConnection: "
                        << result_or_error.error().message();
    }
  } else {
    RTC_LOG(LS_ERROR) << "OpenVideoCaptureDevice failed";
  }
}


void Conductor::OnMessage(rtc::Message* msg) {
    MessageData *msg_data = (MessageData*)(msg->pdata);
    void *data = NULL;
    if (msg_data) {
        data = msg_data->data_;
        delete msg_data;
    }
    
    switch (msg->message_id) {
      case SEND_MESSAGE_TO_PEER: {
          RTC_LOG(INFO) << "SEND_MESSAGE_TO_PEER";
          std::string* msg = reinterpret_cast<std::string*>(data);
          if (msg) {
              // For convenience, we always run the message through the queue.
              // This way we can be sure that messages are sent to the server
              // in the same order they were signaled without much hassle.
              pending_messages_.push_back(msg);
          }
       
          if (!pending_messages_.empty()) {
              msg = pending_messages_.front();
              pending_messages_.pop_front();
              
              if (!this->SendToPeer(*msg)) {
                  RTC_LOG(LS_ERROR) << "SendToPeer failed";
              }
              delete msg;
          }
          break;
      }
       
      case NEW_STREAM_ADDED: {
          webrtc::MediaStreamInterface* stream =
              reinterpret_cast<webrtc::MediaStreamInterface*>(
                                                              data);
          webrtc::VideoTrackVector tracks = stream->GetVideoTracks();
          // Only render the first track.
          if (!tracks.empty()) {
              webrtc::VideoTrackInterface* track = tracks[0];
              StartRemoteRenderer(track);
          }   
          stream->Release();
          break;
      }
       
      case STREAM_REMOVED: {
          // Remote peer stopped sending a stream.
          webrtc::MediaStreamInterface* stream =
              reinterpret_cast<webrtc::MediaStreamInterface*>(
                                                              data);
           
          stream->Release();
          break;
      }
       
      default:
          RTC_NOTREACHED();
          break;
    }

    this->Release();
}

bool Conductor::SendToPeer(const std::string& message) {
    Json::Reader reader;
    Json::Value value;

    if (!reader.parse(message, value)) {
        return false;
    }

    Json::Value json;
    json["p2p"] = value;
    std::string s = rtc::JsonValueToString(json);
    //todo fix json bug
    client_->SendRTMessage(peer_id_, s);
    return true;
}

void Conductor::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
  peer_connection_->SetLocalDescription(
      DummySetSessionDescriptionObserver::Create(), desc);

  std::string sdp;
  desc->ToString(&sdp);

  Json::StyledWriter writer;
  Json::Value jmessage;

  jmessage[kSessionDescriptionTypeName] = desc->type();
  jmessage[kSessionDescriptionSdpName] = sdp;

  RTC_LOG(INFO) << "sdp type:" << jmessage[kSessionDescriptionTypeName];
  RTC_LOG(INFO) << "sdp" << sdp;
  SendMessage(writer.write(jmessage));
}


void Conductor::OnFailure(webrtc::RTCError error) {
  RTC_LOG(LERROR) << ToString(error.type()) << ": " << error.message();
}

void Conductor::SendMessage(const std::string& json_object) {
  std::string* msg = new std::string(json_object);
  this->AddRef();
  main_thread_->Post(RTC_FROM_HERE, this, SEND_MESSAGE_TO_PEER, new MessageData(msg));
}
