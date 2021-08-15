/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/voip/voip_wnd.h"

#include <math.h>

#include "libyuv/convert_argb.h"
#include "api/video/i420_buffer.h"
#include "rtc_base/arraysize.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

#include "examples/voip/conductor.h"


namespace {
    const int kDialDelay = 1000;
    const int kPingDelay = 1000;
}

//voipwnd
VOIPWnd::VOIPWnd(PeerConnectionClient *client, rtc::Thread* main_thread,
                 int64_t uid, std::string& token)
    :state_(0), client_(client),
     uid_(uid), token_(token),
     main_thread_(main_thread) {
    RTC_LOG(INFO) << "register observer...";
    client_->RegisterObserver(this);
}

VOIPWnd::~VOIPWnd() {
    client_->RegisterObserver(NULL);
}

void VOIPWnd::OnMessage(rtc::Message* msg) {
    if (msg->message_id == 1) {
        if (state_ == VOIP_DIALING) {
            RTC_LOG(INFO) << "dial...";            
            SendVOIPCommand(peer_id_, VOIP_COMMAND_DIAL_VIDEO, channel_id_);
            rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kDialDelay,
                                                this, 1);
        }
    } else if (msg->message_id == 2) {
        if (state_ == VOIP_CONNECTED) {
            uint32_t now = rtc::Time32();
            if (now - timestamp_ > 10*1000) {
                RTC_LOG(INFO) << "peer:" << peer_id_ << " timeout";
                OnPeerDisconnected();
                return;
            }
           
            RTC_LOG(INFO) << "send ping command";
            SendVOIPCommand(peer_id_, VOIP_COMMAND_PING, channel_id_);
            rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay,
                                                this, 2);           
        }
    } else if (msg->message_id == 3) {
        OnPeerConnected();
    } else if (msg->message_id == 4) {
        OnPeerDisconnected();
    }
}

void VOIPWnd::OnPeerConnected() {
    //on peer connected
    timestamp_ = rtc::Time32();
    conductor_ = new rtc::RefCountedObject<Conductor>(client_,
                                                      main_thread_, uid_, token_);
    conductor_->AddRef();

    conductor_->SetLocalRenderer(localRender());
    conductor_->SetRemoteRenderer(remoteRender());
    
    conductor_->ConnectToPeer(peer_id_);
}

void VOIPWnd::OnPeerDisconnected() {
    conductor_->OnPeerDisconnected(peer_id_);
    conductor_->Release();
    conductor_  = NULL;
    state_ = VOIP_HANGED_UP;
    RTC_LOG(INFO) << "peer:" << peer_id_ << " disconnected";    
}

void VOIPWnd::HandleVOIPMessage(int64_t sender, int64_t receiver, Json::Value& obj) {
    int64_t cmd = obj["command"].asInt();
    std::string channel_id = obj["channel_id"].asString();
    RTC_LOG(INFO) << "voip:" << cmd << " channel:" << channel_id;


    if (sender != peer_id_) {
        return;
    }
    if (cmd == VOIP_COMMAND_ACCEPT) {
        SendVOIPCommand(peer_id_, VOIP_COMMAND_CONNECTED, channel_id_);
        if (state_ == VOIP_CONNECTED) {
            return;
        }

        state_ = VOIP_CONNECTED;

        rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kPingDelay,
                                            this, 2);

        rtc::Thread::Current()->Post(RTC_FROM_HERE, this, 3);
     
 
    } else if (cmd == VOIP_COMMAND_HANG_UP) {
        if (state_ == VOIP_CONNECTED) {
            rtc::Thread::Current()->Post(RTC_FROM_HERE, this, 4);
         
        }
    } else if (cmd == VOIP_COMMAND_PING) {
        timestamp_ = rtc::Time32();
    }
    return;        
}

void VOIPWnd::HandleP2PMessage(int64_t sender, int64_t receiver, Json::Value& obj) {
    if (state_ == VOIP_CONNECTED) {
        conductor_->OnMessageFromPeer(sender, rtc::JsonValueToString(obj));
    }
}    

void VOIPWnd::HandleRTMessage(int64_t sender, int64_t receiver, std::string& content) {
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
            RTC_LOG(INFO) << "p2p message:" << content;
            HandleP2PMessage(sender, receiver, obj);
        }
    }
}


void VOIPWnd::OnSignedIn() {
    RTC_LOG(INFO) << "signed in";
}

void VOIPWnd::OnServerConnectionFailure() {
    RTC_LOG(INFO) << "Failed to connect to server";
}

void VOIPWnd::OnDisconnected() {
    RTC_LOG(INFO) << "on disconnected";
}



void VOIPWnd::SendVOIPCommand(int64_t peer_id, int voip_cmd,
                              const std::string& channel_id) {
    Json::Value value;
    value["command"] = voip_cmd;
    value["channel_id"] = channel_id;
 
    Json::Value json;
    json["voip"] = value;
    std::string s = rtc::JsonValueToString(json);
    //todo fix json bug
    client_->SendRTMessage(peer_id, s);
}


void VOIPWnd::Dial() {
    RTC_LOG(INFO) << "connect...";

    if (state_ != VOIP_HANGED_UP && state_ != 0) {
        return;
    }
    
    //::UUID uuid;
    //UuidCreate(&uuid);
    //char *str;
    //UuidToStringA(&uuid, (RPC_CSTR*)&str);
    //RTC_LOG(INFO) << "uuid:" << str;
    //std::string channel_id(str);
    //RpcStringFreeA((RPC_CSTR*)&str);

    RTC_LOG(INFO) << "dial...";
    int64_t now = rtc::TimeMillis();
    std::string channel_id(std::to_string(now));
    //todo input by user
    int64_t peer_id = 1;
    SendVOIPCommand(peer_id, VOIP_COMMAND_DIAL_VIDEO, channel_id);

    rtc::Thread::Current()->PostDelayed(RTC_FROM_HERE, kDialDelay,
                                        this, 1);

    channel_id_ = channel_id;
    peer_id_ = peer_id;
    state_ = VOIP_DIALING;    
}
