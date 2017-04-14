#ifndef WEBRTC_EXAMPLES_PEERCONNECTION_CONNECTION_H_
#define WEBRTC_EXAMPLES_PEERCONNECTION_CONNECTION_H_

#include "webrtc/base/sigslot.h"
#include "webrtc/base/asyncsocket.h"
#include "webrtc/examples/im_client/message.h"

class Connection : public sigslot::has_slots<> {
public:
    Connection(rtc::AsyncSocket *sock);
    
    bool SendMessage(Message& msg);

    rtc::AsyncSocket *socket() {
        return sock_;
    }

    std::string GetCameraID() {
        return camera_id_;
    }

    void SetCameraID(std::string camera_id) {
        camera_id_ = camera_id;
    }

public:
    sigslot::signal1<Connection*> SignalCloseEvent;    
    sigslot::signal2<Connection*, Message*> SignalMessageEvent;   
    
private:
    bool Write(uint8_t *buf, int len);
    
    void OnWrite(rtc::AsyncSocket* socket);
    void OnClose(rtc::AsyncSocket* socket, int err);
    void OnRead(rtc::AsyncSocket* socket);

private:
    
    std::string camera_id_;
    rtc::AsyncSocket* sock_;

    int seq_;
    
    //接受缓存
    char data_[128*1024];
    int data_size_;
    
    //待写入socket的缓存,
    uint8_t block_[128*1024];
    int offset_;
    int size_;
};

#endif
