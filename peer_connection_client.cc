/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/examples/voip/peer_connection_client.h"

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

#define MSG_HEARTBEAT 1

#define MSG_AUTH_STATUS 3
#define MSG_IM 4
#define MSG_ACK 5

#define MSG_GROUP_NOTIFICATION 7
#define MSG_GROUP_IM 8

#define MSG_INPUTING 10

#define MSG_PING 13
#define MSG_PONG 14
#define MSG_AUTH_TOKEN 15

#define MSG_RT 17
#define MSG_ENTER_ROOM 18
#define MSG_LEAVE_ROOM 19
#define MSG_ROOM_IM 20
#define MSG_SYSTEM 21
#define MSG_UNREAD_COUNT 22

#define MSG_CUSTOMER 24
#define MSG_CUSTOMER_SUPPORT 25


//客户端->服务端
#define MSG_SYNC  26 //同步消息
//服务端->客服端
#define MSG_SYNC_BEGIN  27
#define MSG_SYNC_END  28
//通知客户端有新消息
#define MSG_SYNC_NOTIFY  29

//客户端->服务端
#define MSG_SYNC_GROUP  30//同步超级群消息
//服务端->客服端
#define MSG_SYNC_GROUP_BEGIN  31
#define MSG_SYNC_GROUP_END  32
//通知客户端有新消息
#define MSG_SYNC_GROUP_NOTIFY  33

//客服端->服务端
#define MSG_SYNC_KEY  34
#define MSG_GROUP_SYNC_KEY 35


#define MSG_VOIP_CONTROL 64

#define PLATFORM_IOS  1
#define PLATFORM_ANDROID 2
#define PLATFORM_WEB 3
#define PLATFORM_LINUX 4

#define HEADER_SIZE 12

namespace {


// Delay between server connection retries, in milliseconds
const int kReconnectDelay = 2000;
// heartbeat 
const int kHeartbeatDelay = 10*1000;

//ping peer
const int kPingDelay = 1000;

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


class Message {
public:
  Message() {}
  ~Message() {}
  //header
  //4字节length + 4字节seq + 1字节cmd + 1字节version + 2字节0
  int length;//body的长度
  int seq;
  int cmd;
  int version;



  //MSG_AUTH_STATUS
  int status;
  

  //MSG_AUTH_TOKEN
  std::string device_id;
  std::string token;
  int platform_id;


  
  //MSG_RT
  int64_t sender;
  int64_t receiver;
  std::string content;
};


PeerConnectionClient::PeerConnectionClient()
  : callback_(NULL),
    resolver_(NULL),
    state_(NOT_CONNECTED),
    my_id_(-1) {

    data_size_ = 0;
    offset_ = 0;
    size_ = 0;

    seq_ = 0;
    
    token_ = "5DfJ5EeMtxDdCYiivzKV9SmmIuOiUb";
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

int PeerConnectionClient::id() const {
  return my_id_;
}

bool PeerConnectionClient::is_connected() const {
  return my_id_ != -1;
}

const Peers& PeerConnectionClient::peers() const {
  return peers_;
}

void PeerConnectionClient::RegisterObserver(
    PeerConnectionClientObserver* callback) {
  RTC_DCHECK(!callback_);
  callback_ = callback;
}

void PeerConnectionClient::Connect(const std::string& client_name) {
  RTC_DCHECK(!client_name.empty());

  if (state_ != NOT_CONNECTED) {
    LOG(WARNING)
        << "The client must not be connected before you can call Connect()";
    callback_->OnServerConnectionFailure();
    return;
  }

  if (client_name.empty()) {
    callback_->OnServerConnectionFailure();
    return;
  }

  LOG(INFO) << "connect...:" << "115.28.44.59:23000";
  server_address_.SetIP("115.28.44.59");
  server_address_.SetPort(23000);
  client_name_ = client_name;

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

bool PeerConnectionClient::SendToPeer(int peer_id, const std::string& message) {
  if (state_ != CONNECTED)
    return false;

  Json::Reader reader;
  Json::Value value;

  if (!reader.parse(message, value)) {
      return false;
  }

  Json::Value json;
  json["p2p"] = value;
  std::string s = rtc::JsonValueToString(json);

  SendRTMessage(s);
  return true;
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
  peers_.clear();
  if (resolver_ != NULL) {
    resolver_->Destroy(false);
    resolver_ = NULL;
  }
  my_id_ = -1;
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
    my_id_ = 10;
    state_ = CONNECTED;
    LOG(INFO) << "on connected";

    rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kHeartbeatDelay, this,
					1);

    rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay, this,
					2);
    SendAuth();
}


static int64_t hton64(int64_t val )
{
    int64_t high, low;
    low = (int64_t)(val & 0x00000000FFFFFFFF);
    val >>= 32;
    high = (int64_t)(val & 0x00000000FFFFFFFF);
    low = htonl( low );
    high = htonl( high );
    
    return (int64_t)low << 32 | high;
}

static int64_t ntoh64(int64_t val )
{
    int64_t high, low;
    low = (int64_t)(val & 0x00000000FFFFFFFF);
    val>>=32;
    high = (int64_t)(val & 0x00000000FFFFFFFF);
    low = ntohl( low );
    high = ntohl( high );
    
    return (int64_t)low << 32 | high;
}

void PeerConnectionClient::WriteInt32(char *p, int32_t t) {
    t = htonl(t);
    memcpy(p, &t, 4);
}

void PeerConnectionClient::WriteInt64(char *p, int64_t t) {
    t = hton64(t);
    memcpy(p, &t, 8);
}

int32_t PeerConnectionClient::ReadInt32(char *p) {
    int32_t t;
    memcpy(&t, p, 4);
    return ntohl(t);
}

int64_t PeerConnectionClient::ReadInt64(char *p) {
    int64_t t;
    memcpy(&t, p, 8);
    return ntoh64(t);
}

void PeerConnectionClient::ReadHeader(char *p, Message *m) {
    int32_t t;
    memcpy(&t, p, 4);
    m->length = ntohl(t);
    p += 4;
    
    memcpy(&t, p, 4);
    m->seq = ntohl(t);
    p += 4;

    m->cmd = *p++;
    m->version = *p++;
}

bool PeerConnectionClient::ReadMessage(char *p, int size, Message& m) {
    if (size < HEADER_SIZE) {
        return false;
    }

    ReadHeader(p, &m);
    if (m.length > (size - HEADER_SIZE)) {
        return false;
    }
    
    p = p + HEADER_SIZE;
    if (m.cmd == MSG_AUTH_STATUS) {
        //assert m.length == 4
        m.status = ReadInt32(p);
    } else if (m.cmd == MSG_RT) {
        m.sender = ReadInt64(p);
        p += 8;
        m.receiver = ReadInt64(p);
        p += 8;
        m.content.assign(p, m.length - 16);
    }
    return true;
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
            HandleRTMessage(m);
        } else if (m.cmd == MSG_PONG) {
  	    HandlePong(m);
	}
    }

    data_size_ -= offset;
}

void PeerConnectionClient::SendRTMessage(std::string content) {
    Message m;
    m.cmd = MSG_RT;
    m.sender = my_id_;
    //todo
    m.receiver = 1;
    m.content = content;
    SendMessage(m);    
}

void PeerConnectionClient::SendVOIPCommand(int voip_cmd, const std::string& channel_id) {
    Json::Value value;
    value["command"] = voip_cmd;
    value["channel_id"] = channel_id;

    Json::Value json;
    json["voip"] = value;
    std::string s = rtc::JsonValueToString(json);
    
    SendRTMessage(s);
}

void PeerConnectionClient::HandleRTMessage(Message& msg) {
     Json::Reader reader;
     Json::Value value;

     if (reader.parse(msg.content, value)) {
         Json::Value obj;
         bool r;
         r = rtc::GetValueFromJsonObject(value, "voip", &obj);
         if (r) {
             int64_t cmd = obj["command"].asInt();
             std::string channel_id = obj["channel_id"].asString();
             LOG(INFO) << "voip:" << cmd << "channel:" << channel_id;

             if (cmd == VOIP_COMMAND_DIAL_VIDEO) {
                 //auto accept
                 SendVOIPCommand(VOIP_COMMAND_ACCEPT, channel_id);
             } else if (cmd == VOIP_COMMAND_CONNECTED) {
                 //连接成功
                 channel_id_ = channel_id;
                 peers_[msg.sender] = "unknown";
                 callback_->OnPeerConnected(msg.sender, "unknown");
             } else if (cmd == VOIP_COMMAND_HANG_UP) {
                 //对方挂断
                 peers_.erase(msg.sender);
                 channel_id_ = "";
                 callback_->OnPeerDisconnected(msg.sender);
             }
             return;
         }

         r = rtc::GetValueFromJsonObject(value, "p2p", &obj);
         if (r) {
             LOG(INFO) << "p2p message:" << msg.content;
             callback_->OnMessageFromPeer(msg.sender, rtc::JsonValueToString(obj));
             return;
         }
     }
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
  LOG(INFO) << __FUNCTION__;

  socket->Close();

#ifdef WIN32
    if (err != WSAECONNREFUSED) {
#else
    if (err != ECONNREFUSED) {
#endif
        callback_->OnMessageSent(err);
    } else {
        if (socket == control_socket_.get()) {
            LOG(WARNING) << "Connection refused; retrying in 2 seconds";
            rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kReconnectDelay, this,
                                                0);
        }
    }
}

void PeerConnectionClient::OnMessage(rtc::Message* msg) {
  if (msg->message_id == 0) {
      DoConnect();
  } else if (msg->message_id == 1) {
      SendPing();
      rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kHeartbeatDelay, this,
                                          1);    
  } else if (msg->message_id == 2) {
       if (peers_.size() > 0 && channel_id_.length() > 0) {
         SendVOIPCommand(VOIP_COMMAND_PING, channel_id_);
       }
       rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay, this,
					2);
  }
}


bool PeerConnectionClient::SendMessage(Message& msg) {
    if (control_socket_ == NULL) {
        return false;
    }

    msg.seq = ++seq_;
    
    char buf[64*1024] = {0};
    char *p = buf;

    int body_len = 0;
    WriteInt32(p, body_len);
    p += 4;
    WriteInt32(p, msg.seq);
    p += 4;

    *p++ = msg.cmd;
    *p++ = msg.version;
    *p++ = 0;
    *p++ = 0;

    if (msg.cmd == MSG_AUTH_TOKEN) {
        *p++ = msg.platform_id;
        
        *p++ = msg.token.length();
        memcpy(p, msg.token.c_str(), msg.token.length());
        p += msg.token.length();
        
        *p++ = msg.device_id.length();
        memcpy(p, msg.device_id.c_str(), msg.device_id.length());
        p += msg.device_id.length();

        body_len = 1 + 1 + msg.token.length() + 1 + msg.device_id.length();
    } else if (msg.cmd == MSG_RT) {
        WriteInt64(p, msg.sender);
        p += 8;
        WriteInt64(p, msg.receiver);
        p += 8;
        memcpy(p, msg.content.c_str(), msg.content.length());
        p += msg.content.length();

        body_len = 8 + 8 + msg.content.length();
    }
    
    //重写消息体长度
    WriteInt32(buf, body_len);
    
    int len = p - buf;
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
