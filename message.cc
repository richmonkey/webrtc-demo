#include "webrtc/examples/im_client/message.h"
#include <string.h>
#include <arpa/inet.h>

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

static void WriteInt32(char *p, int32_t t) {
    t = htonl(t);
    memcpy(p, &t, 4);
}

static void WriteInt64(char *p, int64_t t) {
    t = hton64(t);
    memcpy(p, &t, 8);
}

static int32_t ReadInt32(char *p) {
    int32_t t;
    memcpy(&t, p, 4);
    return ntohl(t);
}

static int64_t ReadInt64(char *p) {
    int64_t t;
    memcpy(&t, p, 8);
    return ntoh64(t);
}


void ReadHeader(char *p, Message *m) {
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

    
    
bool ReadMessage(char *p, int size, Message& m) {
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

//todo 校验缓冲区是否越界
int WriteMessage(char *buf, int size, Message& msg) {
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

    //assert(body_len <= size);
    return body_len + HEADER_SIZE;
}
