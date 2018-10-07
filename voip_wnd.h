/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_VOIP_WND_H_
#define WEBRTC_EXAMPLES_PEERCONNECTION_CLIENT_VOIP_WND_H_


#include <map>
#include <memory>
#include <string>

#include "api/mediastreaminterface.h"
#include "api/video/video_frame.h"
#include "rtc_base/json.h"
#include "media/base/mediachannel.h"
#include "media/base/videocommon.h"
#include "examples/voip/peer_connection_client.h"
#include "examples/voip/conductor.h"


class VOIPWnd : public rtc::MessageHandler,
    public PeerConnectionClientObserver {
 public:

    VOIPWnd(PeerConnectionClient* client,
            rtc::Thread* main_thread,
            int64_t uid, std::string& token);

    virtual ~VOIPWnd();

    void Dial();
    //PeerConnectionObserver implement
    virtual void OnSignedIn();  
    virtual void OnDisconnected();
    virtual void OnServerConnectionFailure();
    virtual void HandleRTMessage(int64_t sender,
                                 int64_t receiver,
                                 std::string& content);
 protected:

    virtual rtc::VideoSinkInterface<webrtc::VideoFrame> *localRender() = 0;
    virtual rtc::VideoSinkInterface<webrtc::VideoFrame> *remoteRender() = 0;
    

        
    virtual void OnPeerConnected();
    virtual void OnPeerDisconnected();
    
    void SendVOIPCommand(int64_t peer_id, int voip_cmd,
                         const std::string& channel_id);
    void OnMessage(rtc::Message* msg);

    void HandleVOIPMessage(int64_t sender, int64_t receiver, Json::Value& obj);
    void HandleP2PMessage(int64_t sender, int64_t receiver, Json::Value& obj);



    // A little helper class to make sure we always to proper locking and
    // unlocking when working with VideoRenderer buffers.
    template <typename T>
        class AutoLock {
    public:
        explicit AutoLock(T* obj) : obj_(obj) { obj_->Lock(); }
        ~AutoLock() { obj_->Unlock(); }
    protected:
        T* obj_;
    };

  
    enum VOIPState {
        VOIP_DIALING = 1,
        VOIP_CONNECTED,
        VOIP_ACCEPTING,
        VOIP_ACCEPTED,
        VOIP_REFUSED,
        VOIP_HANGED_UP,
        VOIP_SHUTDOWN,
    };
  
    int state_;

    std::string channel_id_;
    int64_t peer_id_;

    rtc::RefCountedObject<Conductor> *conductor_;
    PeerConnectionClient *client_;
    int64_t uid_;
    std::string token_;
    rtc::Thread *main_thread_;
    uint32_t timestamp_; //last received ping timestamp, unit:ms
};

#endif
