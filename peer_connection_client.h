/*
 *  Copyright 2011 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_PEER_CONNECTION_CLIENT_H_
#define WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_PEER_CONNECTION_CLIENT_H_

#include <map>
#include <memory>
#include <string>

#include "webrtc/base/nethelpers.h"
#include "webrtc/base/physicalsocketserver.h"
#include "webrtc/base/signalthread.h"
#include "webrtc/base/sigslot.h"

enum EVOIPCommand {
    //语音通话
    VOIP_COMMAND_DIAL = 1,
    VOIP_COMMAND_ACCEPT = 2,
    VOIP_COMMAND_CONNECTED = 3,
    VOIP_COMMAND_REFUSE = 4,
    VOIP_COMMAND_REFUSED = 5,
    VOIP_COMMAND_HANG_UP = 6,
    VOIP_COMMAND_RESET = 7,

    //通话中
    VOIP_COMMAND_TALKING = 8,
    
    //视频通话
    VOIP_COMMAND_DIAL_VIDEO = 9,
    
    VOIP_COMMAND_PING = 10,
};

struct PeerConnectionClientObserver {
  virtual void OnSignedIn() = 0;  // Called when we're logged on.
  virtual void OnDisconnected() = 0;
  virtual void OnServerConnectionFailure() = 0;
    
  virtual void HandleRTMessage(int64_t sender,
                               int64_t receiver,
                               std::string& content) = 0;
    
 protected:
  virtual ~PeerConnectionClientObserver() {}
};

class Message;
class PeerConnectionClient : public sigslot::has_slots<>,
                             public rtc::MessageHandler {
 public:
  enum State {
    NOT_CONNECTED,
    RESOLVING,
    SIGNING_IN,
    CONNECTED,
    SIGNING_OUT,
  };

  PeerConnectionClient();
  ~PeerConnectionClient();

  int64_t id() const;
  void setID(int64_t id);
  void setToken(std::string& token);
  
  bool is_connected() const;
  void RegisterObserver(PeerConnectionClientObserver *ob);

  void Connect();
  bool SignOut();

  void SendRTMessage(int64_t peer_id, std::string content);
    
  // implements the MessageHandler interface
  void OnMessage(rtc::Message* msg);
  
 protected:
  void DoResolveOrConnect();
  void DoConnect();
  void Close();
  void InitSocketSignals();
  bool ConnectControlSocket();
  void OnConnect(rtc::AsyncSocket* socket);
  void OnMessageFromPeer(int peer_id, const std::string& message);


  void OnWrite(rtc::AsyncSocket* socket);
  void OnRead(rtc::AsyncSocket* socket);

  void OnClose(rtc::AsyncSocket* socket, int err);

  void OnResolveResult(rtc::AsyncResolverInterface* resolver);


  void HandlePong(Message& msg);
  void SendAuth();

  void SendPing();
  bool SendMessage(Message& msg);
    



  PeerConnectionClientObserver* callback_;
  rtc::SocketAddress server_address_;
  rtc::AsyncResolver* resolver_;
  std::unique_ptr<rtc::AsyncSocket> control_socket_;
  State state_;
  int64_t my_id_;
  
  char data_[128*1024];
  int data_size_;
  
  std::string token_;
  
  //待写入socket的缓存,
  uint8_t block_[128*1024];
  int offset_;
  int size_;

  int seq_;

  int64_t ping_ts_;
};

#endif  // WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_PEER_CONNECTION_CLIENT_H_
