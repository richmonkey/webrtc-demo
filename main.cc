
#include "webrtc/examples/voip/flagdefs.h"
#include "webrtc/examples/voip/conductor.h"
#include "webrtc/examples/voip/peer_connection_client.h"
#include "webrtc/base/ssladapter.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/physicalsocketserver.h"


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

  thread->Run();

  thread->set_socketserver(NULL);
  
  rtc::CleanupSSL();
  return 0;
}


