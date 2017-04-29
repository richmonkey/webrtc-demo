#include "webrtc/examples/im_client/peer_connection_client.h"
#include "webrtc/examples/im_client/message.h"
#include "webrtc/examples/im_client/connection.h"
#include "webrtc/examples/im_client/load.h"

#include "webrtc/base/ssladapter.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/base/physicalsocketserver.h"
#include "webrtc/base/json.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LISTEN_IP "0.0.0.0" //127.0.0.1
#define LISTEN_PORT 19999

#define TOKEN "5DfJ5EeMtxDdCYiivzKV9SmmIuOiUb"
#define ID 10


struct Camera {
    char id[256];
    char ip[256];
    char username[256];
    char password[256];
    int port;
};

const int camera_count = 4;
Camera cameras[4];


//检查voip子进程的定时器
const int kCheckCameraDelay = 30*1000;
const int kFirstCheckCameraDelay = 1000;

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


        rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kFirstCheckCameraDelay,
                                            this, 1);
        
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
            r = rtc::GetStringFromJsonObject(value, "camera_id", &camera_id);
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
        if (msg->message_id == 1) {
            rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kCheckCameraDelay,
                                                this, 1);
            CheckCamera();
        }
    }

    //检查voip子进程是否存在,确保每个摄像头对应一个voip进程
    void CheckCamera() {
        for (int i = 0; i < camera_count; i++) {
            Camera& camera = cameras[i];
            bool found = false;
            for (size_t i = 0; i < connections_.size(); i++) {
                Connection *conn = connections_[i];
                if (conn->GetCameraID() == camera.id) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                char id[64] = {0};
                char port[64] = {0};
                snprintf(id, sizeof(id), "%d", ID);
                snprintf(port, sizeof(port), "%d", camera.port);
                printf("id:%s", id);
                //启动voip进程
                pid_t pid = fork();
                if (pid == 0) {
                    //fork之后必须立刻调用exec
                    int r = execl("voip", "voip",  "-d", id, "-I", camera.ip,
                                  "-P", port, "-u", camera.username,
                                  "-p", camera.password, "-c", camera.id,
                                  "-t", TOKEN, NULL);
                    if (r == -1) {
                        LOG(INFO) << "execl error:" << errno;
                        exit(1);
                    }
                } else if (pid > 0) {
                    LOG(INFO) << "fork success:" << pid;
                } else {
                    LOG(INFO) << "fork fail:" << errno;
                }
            }
        }
    }

    
    //listen socket event
    void OnAccept(rtc::AsyncSocket* socket) {
        rtc::SocketAddress addr;
        rtc::AsyncSocket *sock = socket->Accept(&addr);
        if (sock) {
            rtc::SocketDispatcher* sd = (rtc::SocketDispatcher*)sock;
            int fd = sd->GetDescriptor();
            int flags = fcntl(fd, F_GETFD);  
            flags |= FD_CLOEXEC;  
            fcntl(fd, F_SETFD, flags);
                
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
        UpdateCameraList();
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

    void UpdateCameraList() {
        std::vector<std::string> cameras;
        for (size_t i = 0; i < connections_.size(); i++) {
            Connection *c = connections_[i];
            std::string camera_id = c->GetCameraID();
            if (!camera_id.empty()) {
                cameras.push_back(camera_id);
            }
        }
        SetCameraList(cameras);
    }
    
    void OnPipeMessage(Connection*conn, Message *msg) {
        if (msg->cmd == MSG_REGISTER_CAMERA) {
            LOG(INFO) << "register camera id:" << msg->camera_id;
            conn->SetCameraID(msg->camera_id);
            UpdateCameraList();
        } else if (msg->cmd == MSG_RT) {
            LOG(INFO) << "transfer pipe rt message:" << msg->content;
            client_->SendRTMessage(msg->receiver, msg->content);
        }
    }
    
};



int main(int argc, char* argv[]) {
    //todo 动态更新camera的信息
    //camera的静态配置信息
    strcpy(cameras[0].id, "camera1");
    strcpy(cameras[0].ip, "192.168.2.158");
    strcpy(cameras[0].username, "admin");
    strcpy(cameras[0].password, "admin");
    cameras[0].port = 80;

    strcpy(cameras[1].id, "camera2");
    strcpy(cameras[1].ip, "192.168.2.151");
    strcpy(cameras[1].username, "admin");
    strcpy(cameras[1].password, "admin");
    cameras[1].port = 80;


    strcpy(cameras[2].id, "camera3");
    strcpy(cameras[2].ip, "192.168.2.152");
    strcpy(cameras[2].username, "admin");
    strcpy(cameras[2].password, "admin");
    cameras[2].port = 80;

    strcpy(cameras[3].id, "camera4");
    strcpy(cameras[3].ip, "192.168.2.153");
    strcpy(cameras[3].username, "admin");
    strcpy(cameras[3].password, "admin");
    cameras[3].port = 80;

    
    
    
    std::vector<std::string> cameras;
    SetCameraList(cameras);
    
    rtc::AutoThread auto_thread;
    rtc::Thread* thread = rtc::Thread::Current();
    rtc::InitializeSSL();
  
    PeerConnectionClient client;
  
    rtc::AsyncSocket *sock = thread->socketserver()->CreateAsyncSocket(AF_INET, SOCK_STREAM);

    rtc::SocketDispatcher* sd = (rtc::SocketDispatcher*)sock;
    int fd = sd->GetDescriptor();
    int flags = fcntl(fd, F_GETFD);  
    flags |= FD_CLOEXEC;  
    fcntl(fd, F_SETFD, flags);  

    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        LOG(INFO) << "set reuseaddr error:" << errno;
    }
    
    rtc::SocketAddress addr;
    addr.SetIP(LISTEN_IP);
    addr.SetPort(LISTEN_PORT);
    int r = sock->Bind(addr);
    if (r != 0) {
        LOG(INFO) << "bind fail";
        return r;
    }

    r = sock->Listen(10);
    if (r != 0) {
        LOG(INFO) << "listen fail";
        return r;
    }


  
    Main m(&client, sock);
    //todo 注册登录当前设备
    std::string token = TOKEN;
    client.setToken(token);
    client.setID(ID);//当前uid
    client.Connect();


    
    thread->Run();

    thread->set_socketserver(NULL);
  
    rtc::CleanupSSL();
    return 0;
}


