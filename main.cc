#include "webrtc/examples/im_client/peer_connection_client.h"
#include "webrtc/examples/im_client/message.h"
#include "webrtc/examples/im_client/connection.h"

#include "webrtc/base/ssladapter.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/base/physicalsocketserver.h"
#include "webrtc/base/json.h"


class Main: public rtc::MessageHandler,
            public PeerConnectionClientObserver,
            public sigslot::has_slots<> {
public:
    Main(PeerConnectionClient* client, rtc::AsyncSocket* sock)
        :client_(client),
         connected_(false),
         listen_sock_(sock) {
        client->RegisterObserver(this);

        listen_sock_->SignalReadEvent.connect(this,
                                              &Main::OnAccept);
    }
    ~Main() {}
    
private:
    PeerConnectionClient* client_;
    bool connected_;

    std::unique_ptr<rtc::AsyncSocket> listen_sock_;
    std::vector<Connection*> connections_;
    
private:
    //
    // PeerConnectionClientObserver implementation.
    //
    void OnSignedIn() {
        LOG(INFO) << __FUNCTION__;
        connected_ = true;
    }

    void OnDisconnected() {
        LOG(INFO) << __FUNCTION__;
        connected_ = false;
        
    }

    void OnServerConnectionFailure() {
        LOG(INFO) << "Failed to connect to server";
    }

     
    void HandleRTMessage(int64_t sender, int64_t receiver, std::string& content) {
        Json::Reader reader;
        Json::Value value;

        LOG(INFO) << "rt message:" << content;
        
        if (reader.parse(content, value)) {
            bool r;
            std::string camera_id;
            r = rtc::GetStringFromJson(value, &camera_id);
            if (!r) {
                return;
            }
            
            for (size_t i = 0; i < connections_.size(); i++) {
                Connection* conn = connections_[i];
                if (conn->GetCameraID() == camera_id) {
                    //转发给camera对应的voip进程
                    Message m;
                    m.cmd = MSG_RT;
                    m.sender = sender;
                    m.receiver = receiver;
                    m.content = content;
                    conn->SendMessage(m);
                }
            }
        }
    }

    
    void OnMessage(rtc::Message* msg) {
     
    }

    //listen socket event
    void OnAccept(rtc::AsyncSocket* socket) {
        rtc::SocketAddress addr;
        rtc::AsyncSocket *sock = socket->Accept(&addr);
        if (sock) {
            Connection *conn = new Connection(sock);
            conn->SignalCloseEvent.connect(this, &Main::OnClose);
            conn->SignalMessageEvent.connect(this, &Main::OnPipeMessage);
                
            connections_.push_back(conn);
        }
    }


    // connection event
    void OnClose(Connection* connection) {
        int position = FindConnection(connection);
        if (position == -1) {
            LOG(INFO) << "can't find connection";
            return;
        }

        connections_.erase(connections_.begin() + position);
    }

    int FindConnection(Connection* conn) {
        for (size_t i = 0; i < connections_.size(); i++) {
            Connection *c = connections_[i];
            if (c == conn) {
                return i;
            }
        }
        return -1;
    }

   
    void OnPipeMessage(Connection*conn, Message *msg) {
        if (msg->cmd == MSG_REGISTER_CAMERA) {
            conn->SetCameraID(msg->camera_id);
        } else if (msg->cmd == MSG_RT) {
            client_->SendRTMessage(msg->receiver, msg->content);
        }
    }
    
};


int main(int argc, char* argv[]) {
  rtc::AutoThread auto_thread;
  rtc::Thread* thread = rtc::Thread::Current();
  rtc::InitializeSSL();
  
  PeerConnectionClient client;
  
  rtc::AsyncSocket *sock = thread->socketserver()->CreateAsyncSocket(AF_INET, SOCK_STREAM);
  rtc::SocketAddress addr;
  addr.SetPort(19999);
  int r = sock->Bind(addr);
  if (r != 0) {
      LOG(INFO) << "bind fail";
  } else {
      LOG(INFO) << "bind success";
  }
  
  Main m(&client, sock);
  //todo 注册登录当前设备
  std::string token = "5DfJ5EeMtxDdCYiivzKV9SmmIuOiUb";
  client.setToken(token);
  client.setID(10);//当前uid
  client.Connect();
  
  thread->Run();

  thread->set_socketserver(NULL);
  
  rtc::CleanupSSL();
  return 0;
}


