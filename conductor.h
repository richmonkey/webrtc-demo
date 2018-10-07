/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_CONDUCTOR_H_
#define WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_CONDUCTOR_H_

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "api/mediastreaminterface.h"
#include "api/peerconnectioninterface.h"
#include "examples/voip/peer_connection_client.h"
//#include "examples/voip/video_renderer.h"
//#include "base/win32.h"

namespace webrtc {
class VideoCaptureModule;
}  // namespace webrtc

namespace cricket {
class VideoRenderer;
}  // namespace cricket


class Conductor
    : public webrtc::PeerConnectionObserver,
    public webrtc::CreateSessionDescriptionObserver,
    public rtc::MessageHandler {
 public:
  enum CallbackID {
    SEND_MESSAGE_TO_PEER = 1,
    NEW_STREAM_ADDED,
    STREAM_REMOVED,
  };

  Conductor(PeerConnectionClient* client,
            rtc::Thread* main_thread,
            int64_t uid,
            std::string& token);

  bool connection_active() const;

  virtual void Close();

  void ConnectToPeer(int peer_id);
  void OnPeerConnected(int id, const std::string& name);
  void OnPeerDisconnected(int id);
  void OnMessageFromPeer(int peer_id, const std::string& message);
  void OnServerConnectionFailure();


  //VideoRenderer* GetLocalRenderer() {
  //    return local_renderer_.get();
  //}
  // 
  //VideoRenderer* GetRemoteRenderer() {
  //    return remote_renderer_.get();
  //}

  void SetLocalRenderer(rtc::VideoSinkInterface<webrtc::VideoFrame>* render) {
     local_renderer_ = render;
  }
  
  void SetRemoteRenderer(rtc::VideoSinkInterface<webrtc::VideoFrame>* render) {
      remote_renderer_ = render;
  }
  
 protected:
  ~Conductor();
  bool InitializePeerConnection();
  bool CreatePeerConnection(bool dtls);
  void DeletePeerConnection();
  void EnsureStreamingUI();
  void AddTracks();
  std::unique_ptr<cricket::VideoCapturer> OpenVideoCaptureDevice();

  void StartLocalRenderer(webrtc::VideoTrackInterface* local_video);
  void StopLocalRenderer();
  void StartRemoteRenderer(webrtc::VideoTrackInterface* remote_video);
  void StopRemoteRenderer();
  
  //
  // PeerConnectionObserver implementation.
  //

  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override{};
  void OnAddStream(
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
  void OnRemoveStream(
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override;
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override{};
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override{};
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}


  // CreateSessionDescriptionObserver implementation.
  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  void OnFailure(const std::string& error) override;

  // implements the MessageHandler interface
  void OnMessage(rtc::Message* msg);

 protected:
  // Send a message to the remote peer.
  void SendMessage(const std::string& json_object);
  bool SendToPeer(const std::string& message);
    
  int peer_id_;
  bool loopback_;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  PeerConnectionClient* client_;
 
  std::deque<std::string*> pending_messages_;
  std::string server_;

  rtc::Thread *main_thread_;
  
  std::unique_ptr<rtc::Thread> _networkThread;
  std::unique_ptr<rtc::Thread> _workerThread;
  std::unique_ptr<rtc::Thread> _signalingThread;

  int64_t uid_;
  std::string token_;

  rtc::scoped_refptr<webrtc::VideoTrackInterface>  local_video_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface>  remote_video_track_;  
  
  rtc::VideoSinkInterface<webrtc::VideoFrame>* local_renderer_;
  rtc::VideoSinkInterface<webrtc::VideoFrame>* remote_renderer_;  
  //  std::unique_ptr<VideoRenderer> local_renderer_;
  //  std::unique_ptr<VideoRenderer> remote_renderer_;  
  
};

#endif  // WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_CONDUCTOR_H_
