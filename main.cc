#include "webrtc/examples/im_client/peer_connection_client.h"
#include "webrtc/examples/im_client/main_wnd.h"

#include "webrtc/base/ssladapter.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/base/physicalsocketserver.h"
#include "webrtc/base/win32socketinit.h"
#include "webrtc/base/win32socketserver.h"

#include <stdlib.h>
#include <stdio.h>


#define TOKEN "5DfJ5EeMtxDdCYiivzKV9SmmIuOiUb"
#define ID 10


int main() {
    rtc::EnsureWinsockInit();
    rtc::Win32Thread w32_thread;
    rtc::ThreadManager::Instance()->SetCurrentThread(&w32_thread);
  
    rtc::AutoThread auto_thread;
    rtc::Thread* thread = rtc::Thread::Current();

    rtc::InitializeSSL();
  
    PeerConnectionClient client;
    std::string token = TOKEN;
    client.setToken(token);
    client.setID(ID);//当前uid
    client.Connect();
    
    MainWnd wnd(&client, thread, ID, token);
    if (!wnd.Create()) {
        return -1;
    }
    
    thread->Run();
    rtc::CleanupSSL();
    return 0;
}


