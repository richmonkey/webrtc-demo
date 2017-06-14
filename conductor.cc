/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/examples/im_client/conductor.h"

#include <memory>
#include <utility>
#include <vector>

#include "webrtc/api/test/fakeconstraints.h"
#include "webrtc/base/checks.h"
#include "webrtc/base/json.h"
#include "webrtc/base/logging.h"
#include "webrtc/media/engine/webrtcvideocapturerfactory.h"
#include "webrtc/modules/video_capture/video_capture_factory.h"
#include "webrtc/examples/im_client/defaults.h"


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
    LOG(INFO) << __FUNCTION__;
  }
  virtual void OnFailure(const std::string& error) {
    LOG(INFO) << __FUNCTION__ << " " << error;
  }

 protected:
  DummySetSessionDescriptionObserver() {}
  ~DummySetSessionDescriptionObserver() {}
};

Conductor::Conductor(PeerConnectionClient* client,
                     HWND hwnd,
                     rtc::Thread* main_thread,
                     int64_t uid,
                     std::string& token)
  : peer_id_(-1),
    loopback_(false),
    client_(client),
    hwnd_(hwnd),
    main_thread_(main_thread),
    uid_(uid),
    token_(token) {
    
    _networkThread = rtc::Thread::CreateWithSocketServer();
    bool result = _networkThread->Start();
    if (!result) {
        LOG(INFO) <<"start thread error";
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

  peer_connection_factory_ = webrtc::CreatePeerConnectionFactory( _networkThread.get(),
                                                                  _workerThread.get(),
                                                                  _signalingThread.get(),
                                                                  nullptr, nullptr, nullptr);

  //peer_connection_factory_  = webrtc::CreatePeerConnectionFactory();

  if (!peer_connection_factory_.get()) {
    LOG(WARNING) << "Failed to initialize PeerConnectionFactory";
    DeletePeerConnection();
    return false;
  }

  if (!CreatePeerConnection(DTLS_ON)) {
    LOG(WARNING) << "CreatePeerConnection failed";
    DeletePeerConnection();
  }
  AddStreams();
  return peer_connection_.get() != NULL;
}

bool Conductor::ReinitializePeerConnectionForLoopback() {
  loopback_ = true;
  rtc::scoped_refptr<webrtc::StreamCollectionInterface> streams(
      peer_connection_->local_streams());
  peer_connection_ = NULL;
  if (CreatePeerConnection(DTLS_OFF)) {
    for (size_t i = 0; i < streams->count(); ++i)
      peer_connection_->AddStream(streams->at(i));
    peer_connection_->CreateOffer(this, NULL);
  }
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
  snprintf(s, 64, "7_%lld", uid_);
  server2.username = s;
  server2.password = token_;
  config.servers.push_back(server2);

  webrtc::FakeConstraints constraints;
  if (dtls) {
    constraints.AddOptional(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                            "true");
  } else {
    constraints.AddOptional(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                            "false");
  }

  peer_connection_ = peer_connection_factory_->CreatePeerConnection(
      config, &constraints, NULL, NULL, this);
  return peer_connection_.get() != NULL;
}

void Conductor::DeletePeerConnection() {
  peer_connection_ = NULL;
  active_streams_.clear();
  StopLocalRenderer();
  StopRemoteRenderer();  
  peer_connection_factory_ = NULL;
  peer_id_ = -1;
  loopback_ = false;
}

void Conductor::StartLocalRenderer(webrtc::VideoTrackInterface* local_video) {
  local_renderer_.reset(new VideoRenderer(hwnd_, 1, 1, local_video));
}

void Conductor::StopLocalRenderer() {
  local_renderer_.reset();
}

void Conductor::StartRemoteRenderer(webrtc::VideoTrackInterface* remote_video) {
  remote_renderer_.reset(new VideoRenderer(hwnd_, 1, 1, remote_video));
}

void Conductor::StopRemoteRenderer() {
  remote_renderer_.reset();
}


//
// PeerConnectionObserver implementation.
//

// Called when a remote stream is added
void Conductor::OnAddStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  LOG(INFO) << __FUNCTION__ << " " << stream->label();

  this->AddRef();
  main_thread_->Post(RTC_FROM_HERE, this, NEW_STREAM_ADDED, new MessageData(stream.release()));
}

void Conductor::OnRemoveStream(
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  LOG(INFO) << __FUNCTION__ << " " << stream->label();
  
  this->AddRef();
  main_thread_->Post(RTC_FROM_HERE, this, STREAM_REMOVED, new MessageData(stream.release()));
}

void Conductor::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
  LOG(INFO) << __FUNCTION__ << " " << candidate->sdp_mline_index();
  // For loopback test. To save some connecting delay.
  if (loopback_) {
    if (!peer_connection_->AddIceCandidate(candidate)) {
      LOG(WARNING) << "Failed to apply the received candidate";
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
    LOG(LS_ERROR) << "Failed to serialize candidate";
    return;
  }
  jmessage[kCandidateSdpName] = sdp;
  SendMessage(writer.write(jmessage));
}


void Conductor::OnPeerConnected(int id, const std::string& name) {
    LOG(INFO) << __FUNCTION__;
    if (!peer_connection_.get()) {
        RTC_DCHECK(peer_id_ == -1);
        peer_id_ = id;

        if (!InitializePeerConnection()) {
            LOG(LS_ERROR) << "Failed to initialize our PeerConnection instance";
            return;
        }
    }  
}

void Conductor::OnPeerDisconnected(int id) {
  LOG(INFO) << __FUNCTION__;
  if (id == peer_id_) {
    LOG(INFO) << "Our peer disconnected";
    DeletePeerConnection();
    RTC_DCHECK(active_streams_.empty());
  }
}

void Conductor::OnMessageFromPeer(int peer_id, const std::string& message) {
  RTC_DCHECK(peer_id_ == peer_id || peer_id_ == -1);
  RTC_DCHECK(!message.empty());

  if (!peer_connection_.get()) {
    RTC_DCHECK(peer_id_ == -1);
    peer_id_ = peer_id;

    if (!InitializePeerConnection()) {
      LOG(LS_ERROR) << "Failed to initialize our PeerConnection instance";
      return;
    }
  } else if (peer_id != peer_id_) {
    RTC_DCHECK(peer_id_ != -1);
    LOG(WARNING) << "Received a message from unknown peer while already in a "
                    "conversation with a different peer.";
    return;
  }

  Json::Reader reader;
  Json::Value jmessage;
  if (!reader.parse(message, jmessage)) {
    LOG(WARNING) << "Received unknown message. " << message;
    return;
  }
  std::string type;
  std::string json_object;

  rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionTypeName, &type);
  if (type == "offer" || type == "answer" || type == "offer-loopback") {
    std::string sdp;
    if (!rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionSdpName,
                                      &sdp)) {
      LOG(WARNING) << "Can't parse received session description message.";
      return;
    }
    webrtc::SdpParseError error;
    webrtc::SessionDescriptionInterface* session_description(
        webrtc::CreateSessionDescription(type, sdp, &error));
    if (!session_description) {
      LOG(WARNING) << "Can't parse received session description message. "
          << "SdpParseError was: " << error.description;
      return;
    }
    LOG(INFO) << " Received session description :" << message;
    peer_connection_->SetRemoteDescription(
        DummySetSessionDescriptionObserver::Create(), session_description);
    if (session_description->type() ==
        webrtc::SessionDescriptionInterface::kOffer) {
      peer_connection_->CreateAnswer(this, NULL);
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
      LOG(WARNING) << "Can't parse received message.";
      return;
    }
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> candidate(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));
    if (!candidate.get()) {
      LOG(WARNING) << "Can't parse received candidate message. "
          << "SdpParseError was: " << error.description;
      return;
    }
    if (!peer_connection_->AddIceCandidate(candidate.get())) {
      LOG(WARNING) << "Failed to apply the received candidate";
      return;
    }
    LOG(INFO) << " Received candidate :" << message;
    return;
  } else {
    LOG(WARNING) << "unknown type:" << type;
  }
}


void Conductor::OnServerConnectionFailure() {
    LOG(INFO) << "Failed to connect to server";
}

void Conductor::ConnectToPeer(int peer_id) {
    RTC_DCHECK(peer_id_ == -1);
    RTC_DCHECK(peer_id != -1);
    
    if (peer_connection_.get()) {
        LOG(INFO) << "We only support connecting to one peer at a time";
        return;
    }

    if (InitializePeerConnection()) {
        peer_id_ = peer_id;
        peer_connection_->CreateOffer(this, NULL);
    } else {
        LOG(INFO) << "Failed to initialize PeerConnection";
    }
}

std::unique_ptr<cricket::VideoCapturer> Conductor::OpenVideoCaptureDevice() {
    std::vector<std::string> device_names;
    {
        std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(webrtc::VideoCaptureFactory::CreateDeviceInfo());
        if (!info) {
            return nullptr;
        }
        int num_devices = info->NumberOfDevices();
        for (int i = 0; i < num_devices; ++i) {
            const uint32_t kSize = 256;
            char name[kSize] = {0};
            char id[kSize] = {0};
            if (info->GetDeviceName(i, name, kSize, id, kSize) != -1) {
                device_names.push_back(name);
            }
        }
    }
  
    cricket::WebRtcVideoDeviceCapturerFactory factory;
    std::unique_ptr<cricket::VideoCapturer> capturer;
    for (const auto& name : device_names) {
        capturer = factory.Create(cricket::Device(name, 0));
        if (capturer) {
            break;
        }
    }
  
    return capturer;
}

void Conductor::AddStreams() {
  if (active_streams_.find(kStreamLabel) != active_streams_.end())
    return;  // Already added.

  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
      peer_connection_factory_->CreateAudioTrack(
          kAudioLabel, peer_connection_factory_->CreateAudioSource(NULL)));

  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
      peer_connection_factory_->CreateVideoTrack(
          kVideoLabel,
          peer_connection_factory_->CreateVideoSource(OpenVideoCaptureDevice(),
                                                      NULL)));
  StartLocalRenderer(video_track);
    
  rtc::scoped_refptr<webrtc::MediaStreamInterface> stream =
      peer_connection_factory_->CreateLocalMediaStream(kStreamLabel);

  stream->AddTrack(audio_track);
  stream->AddTrack(video_track);
  if (!peer_connection_->AddStream(stream)) {
    LOG(LS_ERROR) << "Adding stream to PeerConnection failed";
  }
  typedef std::pair<std::string,
                    rtc::scoped_refptr<webrtc::MediaStreamInterface> >
      MediaStreamPair;
  active_streams_.insert(MediaStreamPair(stream->label(), stream));
 
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
          LOG(INFO) << "SEND_MESSAGE_TO_PEER";
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
                  LOG(LS_ERROR) << "SendToPeer failed";
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

  LOG(INFO) << "sdp type:" << jmessage[kSessionDescriptionTypeName];
  LOG(INFO) << "sdp" << sdp;
  SendMessage(writer.write(jmessage));
}

void Conductor::OnFailure(const std::string& error) {
    LOG(LERROR) << error;
}

void Conductor::SendMessage(const std::string& json_object) {
  std::string* msg = new std::string(json_object);
  this->AddRef();
  main_thread_->Post(RTC_FROM_HERE, this, SEND_MESSAGE_TO_PEER, new MessageData(msg));
}
