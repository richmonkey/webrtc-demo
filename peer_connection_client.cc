/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/examples/im_client/peer_connection_client.h"
#include "webrtc/examples/im_client/message.h"

#include "webrtc/examples/voip/defaults.h"
#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/nethelpers.h"
#include "webrtc/base/stringutils.h"
#include "webrtc/base/json.h"

#ifdef WIN32
#include "webrtc/base/win32socketserver.h"
#endif

using rtc::sprintfn;

namespace {


// Delay between server connection retries, in milliseconds
const int kReconnectDelay = 2000;
// heartbeat 
const int kHeartbeatDelay = 10*1000;


rtc::AsyncSocket* CreateClientSocket(int family) {
#ifdef WIN32
  rtc::Win32Socket* sock = new rtc::Win32Socket();
  sock->CreateT(family, SOCK_STREAM);
  return sock;
#elif defined(WEBRTC_POSIX)
  rtc::Thread* thread = rtc::Thread::Current();
  RTC_DCHECK(thread != NULL);
  return thread->socketserver()->CreateAsyncSocket(family, SOCK_STREAM);
#else
#error Platform not supported.
#endif
}

}  // namespace



PeerConnectionClient::PeerConnectionClient()
  : resolver_(NULL),
    state_(NOT_CONNECTED),
    my_id_(-1) {
    
    data_size_ = 0;
    offset_ = 0;
    size_ = 0;

    seq_ = 0;
}

PeerConnectionClient::~PeerConnectionClient() {
}

void PeerConnectionClient::InitSocketSignals() {
  RTC_DCHECK(control_socket_.get() != NULL);

  control_socket_->SignalWriteEvent.connect(this,
      &PeerConnectionClient::OnWrite);
  control_socket_->SignalCloseEvent.connect(this,
      &PeerConnectionClient::OnClose);
  control_socket_->SignalConnectEvent.connect(this,
      &PeerConnectionClient::OnConnect);
  control_socket_->SignalReadEvent.connect(this,
      &PeerConnectionClient::OnRead);
}

void PeerConnectionClient::setID(int64_t id) {
    my_id_ = id;
}

void PeerConnectionClient::setToken(std::string& token) {
    token_ = token;
}

int64_t PeerConnectionClient::id() const {
  return my_id_;
}

bool PeerConnectionClient::is_connected() const {
  return my_id_ != -1;
}


void PeerConnectionClient::RegisterObserver(
    PeerConnectionClientObserver* callback) {
  RTC_DCHECK(!callback_);
  callback_ = callback;
}

void PeerConnectionClient::Connect() {
  if (state_ != NOT_CONNECTED) {
    LOG(WARNING)
        << "The client must not be connected before you can call Connect()";
    callback_->OnServerConnectionFailure();
    return;
  }


  LOG(INFO) << "connect...:" << "115.28.44.59:23000";
  server_address_.SetIP("115.28.44.59");
  server_address_.SetPort(23000);

  rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kHeartbeatDelay, this,
                                      1);
  
  DoResolveOrConnect();
}

void PeerConnectionClient::DoResolveOrConnect() {
  //todo 刷新dns
  if (server_address_.IsUnresolvedIP()) {
    state_ = RESOLVING;
    resolver_ = new rtc::AsyncResolver();
    resolver_->SignalDone.connect(this, &PeerConnectionClient::OnResolveResult);
    resolver_->Start(server_address_);
  } else {
    DoConnect();
  } 
}

void PeerConnectionClient::OnResolveResult(
    rtc::AsyncResolverInterface* resolver) {
  if (resolver_->GetError() != 0) {
    callback_->OnServerConnectionFailure();
    resolver_->Destroy(false);
    resolver_ = NULL;
    state_ = NOT_CONNECTED;
  } else {
    server_address_ = resolver_->address();
    DoConnect();
  }
}

void PeerConnectionClient::DoConnect() {
  control_socket_.reset(CreateClientSocket(server_address_.ipaddr().family()));
  InitSocketSignals();

  LOG(INFO) << "connect control socket....";
  bool ret = ConnectControlSocket();
  if (ret)
    state_ = SIGNING_IN;
  if (!ret) {
    callback_->OnServerConnectionFailure();
  }
}

bool PeerConnectionClient::SignOut() {
  if (state_ == NOT_CONNECTED || state_ == SIGNING_OUT)
    return true;

  Close();
  
  state_ = SIGNING_OUT;

  return true;
}

void PeerConnectionClient::Close() {
  control_socket_->Close();
  if (resolver_ != NULL) {
    resolver_->Destroy(false);
    resolver_ = NULL;
  }
  state_ = NOT_CONNECTED;
}

bool PeerConnectionClient::ConnectControlSocket() {
    RTC_DCHECK(control_socket_->GetState() == rtc::Socket::CS_CLOSED);
    int err = control_socket_->Connect(server_address_);
    if (err == SOCKET_ERROR) {
        Close();
        return false;
    }
    return true;
}

void PeerConnectionClient::OnConnect(rtc::AsyncSocket* socket) {
    state_ = CONNECTED;
    LOG(INFO) << "on connected";
    SendAuth();
}

void PeerConnectionClient::OnRead(rtc::AsyncSocket* socket) {
    do {
        int bytes = socket->Recv(data_ + data_size_, 128*1024 - data_size_, nullptr);
        if (bytes <= 0)
            break;
        data_size_ += bytes;
    } while (true);

    int offset = 0;
    while (true) {
        Message m;
        bool r = ReadMessage(data_+offset, data_size_ - offset, m);
        if (!r) {
            break;
        }

        LOG(INFO) << "recv message:" << m.cmd;
        offset += m.length + HEADER_SIZE;
        //处理消息
        if (m.cmd == MSG_AUTH_STATUS) {
            LOG(INFO) << "auth status:" << m.status;
            callback_->OnSignedIn();
        } else if (m.cmd == MSG_RT) {
            callback_->HandleRTMessage(m.sender, m.receiver, m.content);
        } else if (m.cmd == MSG_PONG) {
  	    HandlePong(m);
	}
    }

    data_size_ -= offset;
}

void PeerConnectionClient::SendRTMessage(int64_t peer_id, std::string content) {
    Message m;
    m.cmd = MSG_RT;
    m.sender = my_id_;
    m.receiver = peer_id;
    m.content = content;
    SendMessage(m);    
}


void PeerConnectionClient::HandlePong(Message& msg) {

}

void PeerConnectionClient::OnWrite(rtc::AsyncSocket* socket) {
    if (size_ > 0) {
        size_t sent = control_socket_->Send(block_ + offset_, size_);
        if (sent > 0) {
            offset_ += sent;
            size_ -= sent;
        }
    }
}

void PeerConnectionClient::OnClose(rtc::AsyncSocket* socket, int err) {
  LOG(INFO) << __FUNCTION__ << "error:" << err;
  state_ = NOT_CONNECTED;
  socket->Close();
  control_socket_.reset(NULL);
  
  LOG(WARNING) << "Connection refused; retrying in 2 seconds";
  rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kReconnectDelay, this,
                                      0);
}

void PeerConnectionClient::OnMessage(rtc::Message* msg) {
  if (msg->message_id == 0) {
      DoResolveOrConnect();
  } else if (msg->message_id == 1) {
      SendPing();
      rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kHeartbeatDelay, this,
                                          1);    
  }
}


bool PeerConnectionClient::SendMessage(Message& msg) {
    if (control_socket_ == NULL || state_ != CONNECTED) {
        return false;
    }

    msg.seq = ++seq_;
    
    char buf[64*1024] = {0};
    int len = WriteMessage(buf, 64*1024, msg);
    
    if (size_ > 0) {
        //socket不可写
        int left = 128*1024 - offset_ - size_;
        if (left < len) {
            memmove(block_, block_ + offset_, size_);
            offset_ = 0;
            left = 128*1024 - size_;
        }
        if (left < len) {
            //error data overflow
            return false;
        }
        memcpy(block_ + offset_, buf, len);
        size_ += len;
    } else {
        int sent = control_socket_->Send(buf, len);
        if ((int)sent < 0 && rtc::IsBlockingError(control_socket_->GetError())) {
            memcpy(block_ + offset_, buf, len);
            size_ += len;
        } else if (sent > 0 && sent < len) {
            memcpy(block_ + offset_, buf + sent, len - sent);
            size_ += (len - sent);
        } else {
            return false;
        }
    }
    return true;
}

#define PLATFORM_ID PLATFORM_LINUX
void PeerConnectionClient::SendAuth() {
    std::string device_id = "0123456789";
    
    Message m;
    m.cmd = MSG_AUTH_TOKEN;
    m.token = token_;
    m.device_id = device_id;
    m.platform_id = PLATFORM_ID;
    SendMessage(m);
}


void PeerConnectionClient::SendPing() {
  Message m;
  m.cmd = MSG_PING;
  LOG(INFO) << "ping...";
  SendMessage(m);
}
