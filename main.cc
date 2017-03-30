#include "webrtc/examples/voip/defaults.h"
#include "webrtc/examples/voip/flagdefs.h"
#include "webrtc/examples/voip/conductor.h"
#include "webrtc/examples/voip/peer_connection_client.h"
#include "webrtc/base/ssladapter.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/base/physicalsocketserver.h"
#include "webrtc/base/json.h"

//ping peer
const int kPingDelay = 1000;
//维护peer的连接对象
class Main: public rtc::MessageHandler,
            public PeerConnectionClientObserver {
public:
    Main(PeerConnectionClient* client, rtc::Thread* main_thread)
        :client_(client),
         main_thread_(main_thread),
         connected_(false) {
        client->RegisterObserver(this);
    }
    ~Main() {}
    
private:
    struct Peer {
        rtc::RefCountedObject<Conductor> *ob_;
        std::string channel_id_;
        std::int64_t peer_id_;
        uint32_t timestamp;//上次收到对方发来的ping包时间戳,单位:毫秒
    };

    typedef std::map<int64_t, Peer*> Peers;

    PeerConnectionClient* client_;
    rtc::Thread* main_thread_;

    Peers peers_;

    bool connected_;
private:
    //
    // PeerConnectionClientObserver implementation.
    //
    void OnSignedIn() {
        LOG(INFO) << __FUNCTION__;
        connected_ = true;

        rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay, this,
                                            2);
    }

    void OnDisconnected() {
        LOG(INFO) << __FUNCTION__;
        connected_ = false;
        
    }

    void OnServerConnectionFailure() {
        LOG(INFO) << "Failed to connect to server";
    }

    void HandleVOIPMessage(int64_t sender, int64_t receiver, Json::Value& obj) {
        int64_t cmd = obj["command"].asInt();
        std::string channel_id = obj["channel_id"].asString();
        LOG(INFO) << "voip:" << cmd << "channel:" << channel_id;

        if (cmd == VOIP_COMMAND_DIAL_VIDEO) {
            //auto accept
            SendVOIPCommand(sender, VOIP_COMMAND_ACCEPT, channel_id);
        } else if (cmd == VOIP_COMMAND_CONNECTED) {
            //连接成功
            if (peers_.find(sender) != peers_.end()) {
                //already exists
            } else {
                Peer *p = new Peer();
                p->ob_ = new rtc::RefCountedObject<Conductor>(client_, main_thread_);
                p->ob_->AddRef();
                        
                p->peer_id_ = sender;
                p->channel_id_ = channel_id;
                p->timestamp = rtc::Time32();
                peers_[sender] = p;
                p->ob_->OnPeerConnected(sender, "unknown");
                LOG(INFO) << "peer:" << sender << " connected";
            }
        } else if (cmd == VOIP_COMMAND_HANG_UP) {
            //对方挂断
            if (peers_.find(sender) != peers_.end()) {
                Peer *p = peers_[sender];
                p->ob_->OnPeerDisconnected(sender);
                peers_.erase(sender);
                p->ob_->Release();
                delete p;
                LOG(INFO) << "peer:" << sender << " disconnected";
            }
        } else if (cmd == VOIP_COMMAND_PING) {
            if (peers_.find(sender) != peers_.end()) {
                Peer *p = peers_[sender];
                p->timestamp = rtc::Time32();
            }
        }
        return;        
    }

    void HandleP2PMessage(int64_t sender, int64_t receiver, Json::Value& obj) {
        if (peers_.find(sender) != peers_.end()) {
            Peer *p = peers_[sender];
            p->ob_->OnMessageFromPeer(sender, rtc::JsonValueToString(obj));
        }
        return;        
    }
    
    void HandleRTMessage(int64_t sender, int64_t receiver, std::string& content) {
        Json::Reader reader;
        Json::Value value;

        if (reader.parse(content, value)) {
            Json::Value obj;
            bool r;
            r = rtc::GetValueFromJsonObject(value, "voip", &obj);
            if (r) {
                HandleVOIPMessage(sender, receiver, obj);
            }

            r = rtc::GetValueFromJsonObject(value, "p2p", &obj);
            if (r) {
                LOG(INFO) << "p2p message:" << content;
                HandleP2PMessage(sender, receiver, obj);
            }
        }
    }

    
    void OnMessage(rtc::Message* msg) {
        if (msg->message_id == 2) {
            //todo check peer ping timestamp
            uint32_t now = rtc::Time32();

            std::vector<int64_t> removed;
            for (Peers::iterator iter = peers_.begin();
                 iter != peers_.end(); iter++) {
                Peer *p = iter->second;
                if (now - p->timestamp > 10*1000) {
                    LOG(INFO) << "now:"  << now << " timestmap:" << p->timestamp;
                    //对方已掉线
                    removed.push_back(iter->first);
                }
            }

            for (size_t i = 0; i < removed.size(); i++) {
                int64_t peer = removed[i];
                Peer *p = peers_[peer];
                peers_.erase(peer);
                p->ob_->OnPeerDisconnected(peer);
                p->ob_->Release();
                delete p;
                LOG(INFO) << "peer:" << peer << " timeout";
            }
            
            for (Peers::iterator iter = peers_.begin();
                 iter != peers_.end(); iter++) {
                Peer *p = iter->second;
                SendVOIPCommand(p->peer_id_, VOIP_COMMAND_PING, p->channel_id_);
            }
            rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay, this,
                                                2);
        }
    }
    
    void SendVOIPCommand(int64_t peer_id, int voip_cmd,
                         const std::string& channel_id) {
        Json::Value value;
        value["command"] = voip_cmd;
        value["channel_id"] = channel_id;

        Json::Value json;
        json["voip"] = value;
        std::string s = rtc::JsonValueToString(json);
        client_->SendRTMessage(peer_id, s);
    }
};


int main(int argc, char* argv[]) {
  rtc::FlagList::SetFlagsFromCommandLine(&argc, argv, true);
  if (FLAG_help) {
    rtc::FlagList::Print(NULL, false);
    return 0;
  }
  
  rtc::AutoThread auto_thread;
  rtc::Thread* thread = rtc::Thread::Current();
  rtc::InitializeSSL();
  
  PeerConnectionClient client;
  rtc::scoped_refptr<Conductor> conductor(
    new rtc::RefCountedObject<Conductor>(&client, thread));

  Main m(&client, thread);
  std::string token = "5DfJ5EeMtxDdCYiivzKV9SmmIuOiUb";
  client.setToken(token);
  client.setID(10);//当前uid
  client.Connect(GetPeerName());
  
  thread->Run();

  thread->set_socketserver(NULL);
  
  rtc::CleanupSSL();
  return 0;
}


