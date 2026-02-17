#include <streamsession.h>
#include <settings.h>
#include <controllermanager.h>
#include <chiaki/base64.h>
#include <chiaki/session.h>
#include <chiaki/time.h>
#include "../../lib/src/utils.h"
#include <QKeyEvent>
#include <cstring>

// Definições de Touchpad para PS4 e PS5
#define PS4_TOUCHPAD_MAX_X 1920.0f
#define PS4_TOUCHPAD_MAX_Y 942.0f
#define PS5_TOUCHPAD_MAX_X 1919.0f
#define PS5_TOUCHPAD_MAX_Y 1079.0f

static void EventCb(ChiakiEvent *event, void *user);

StreamSessionConnectInfo::StreamSessionConnectInfo(
    Settings *settings, ChiakiTarget target, QString host, QByteArray regist_key,
    QByteArray morning, QString initial_login_pin, QString duid, bool fullscreen, bool zoom, bool stretch)
{
    this->key_map = settings->GetControllerMappingForDecoding();
    this->target = target;
    this->host = host;
    this->regist_key = regist_key;
    this->morning = morning;
    this->initial_login_pin = initial_login_pin;
    this->duid = duid;
    this->log_level_mask = settings->GetLogLevelMask();
}

StreamSession::StreamSession(const StreamSessionConnectInfo &connect_info, QObject *parent)
    : QObject(parent), log(this, connect_info.log_level_mask, ""), session_started(false), connected(false)
{
    ChiakiConnectInfo chiaki_connect_info = {};
    chiaki_connect_info.ps5 = chiaki_target_is_ps5(connect_info.target);
    chiaki_connect_info.host = connect_info.host.toUtf8().constData();

    if(chiaki_connect_info.ps5) {
        PS_TOUCHPAD_MAX_X = PS5_TOUCHPAD_MAX_X;
        PS_TOUCHPAD_MAX_Y = PS5_TOUCHPAD_MAX_Y;
    } else {
        PS_TOUCHPAD_MAX_X = PS4_TOUCHPAD_MAX_X;
        PS_TOUCHPAD_MAX_Y = PS4_TOUCHPAD_MAX_Y;
    }

    chiaki_controller_state_set_idle(&keyboard_state);
    chiaki_controller_state_set_idle(&touch_state);

    if(!connect_info.duid.isEmpty()) {
        QByteArray psn_id = QByteArray::fromBase64(connect_info.psn_account_id.toUtf8());
        memcpy(chiaki_connect_info.psn_account_id, psn_id.constData(), CHIAKI_PSN_ACCOUNT_ID_SIZE);
    } else {
        memcpy(chiaki_connect_info.regist_key, connect_info.regist_key.constData(), sizeof(chiaki_connect_info.regist_key));
        memcpy(chiaki_connect_info.morning, connect_info.morning.constData(), sizeof(chiaki_connect_info.morning));
    }

    chiaki_session_init(&session, &chiaki_connect_info, GetChiakiLog());
    chiaki_session_set_event_cb(&session, EventCb, this);

#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
    UpdateGamepads();
#endif
}

StreamSession::~StreamSession() {
    if(session_started) chiaki_session_join(&session);
    chiaki_session_fini(&session);
}

void StreamSession::SendFeedbackState() {
    ChiakiControllerState state;
    chiaki_controller_state_set_idle(&state);
    for(auto controller : controllers) {
        auto c_state = controller->GetState();
        chiaki_controller_state_or(&state, &state, &c_state);
    }
    chiaki_controller_state_or(&state, &state, &keyboard_state);
    chiaki_controller_state_or(&state, &state, &touch_state);
    chiaki_session_set_controller_state(&session, &state);
}

void StreamSession::Event(ChiakiEvent *event) {
    if(event->type == CHIAKI_EVENT_CONNECTED) {
        connected = true; emit ConnectedChanged();
    } else if(event->type == CHIAKI_EVENT_QUIT) {
        connected = false; emit SessionQuit(event->quit.reason, QString::fromUtf8(event->quit.reason_str));
    }
}

static void EventCb(ChiakiEvent *event, void *user) {
    reinterpret_cast<StreamSession *>(user)->Event(event);
}

void StreamSession::Start() { chiaki_session_start(&session); session_started = true; }
void StreamSession::Stop() { chiaki_session_stop(&session); }
