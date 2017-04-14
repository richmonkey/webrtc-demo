#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include <stdint.h>


#define MSG_AUTH_STATUS 3
#define MSG_IM 4
#define MSG_ACK 5

#define MSG_PING 13
#define MSG_PONG 14
#define MSG_AUTH_TOKEN 15

#define MSG_RT 17


//本地进程间通讯
#define MSG_REGISTER_CAMERA 200


#define PLATFORM_IOS  1
#define PLATFORM_ANDROID 2
#define PLATFORM_WEB 3
#define PLATFORM_LINUX 4

#define HEADER_SIZE 12


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

  //MSG_REGISTER_CAMERA
  std::string camera_id;
};

void ReadHeader(char *p, Message *m);
bool ReadMessage(char *p, int size, Message& m);
int WriteMessage(char *p, int size, Message& m);

#endif
