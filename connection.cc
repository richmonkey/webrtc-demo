#include "webrtc/examples/im_client/connection.h"
#include "webrtc/base/logging.h"

Connection::Connection(rtc::AsyncSocket *sock)
    :sock_(sock), seq_(0) {

    sock->SignalWriteEvent.connect(this,
                                   &Connection::OnWrite);
    sock->SignalCloseEvent.connect(this,
                                   &Connection::OnClose);
    sock->SignalReadEvent.connect(this,
                                  &Connection::OnRead);
}



bool Connection::SendMessage(Message& msg) {
    if (sock_ == NULL) {
        return false;
    }
    
    msg.seq = ++seq_;
    char buf[64*1024] = {0};
    int len = WriteMessage(buf, 64*1024, msg);

    return Write((uint8_t*)buf, len);
}


bool Connection::Write(uint8_t *buf, int len) {
    if (sock_ == NULL) {
        return false;
    }
    
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
        int sent = sock_->Send(buf, len);
        if ((int)sent < 0 && rtc::IsBlockingError(sock_->GetError())) {
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
   
void Connection::OnWrite(rtc::AsyncSocket* socket) {
    if (size_ > 0) {
        size_t sent = sock_->Send(block_ + offset_, size_);
        if (sent > 0) {
            offset_ += sent;
            size_ -= sent;
        }
    }
}

void Connection::OnClose(rtc::AsyncSocket* socket, int err) {
    LOG(INFO) << __FUNCTION__ << "error:" << err;
    SignalCloseEvent(this);
}

void Connection::OnRead(rtc::AsyncSocket* socket) {
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
        SignalMessageEvent(this, &m);
        
    }

    data_size_ -= offset;
    

}


