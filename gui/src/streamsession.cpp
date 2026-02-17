// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <streamsession.h>
#include <settings.h>
#include <controllermanager.h>

#include <chiaki/base64.h>
#include <chiaki/streamconnection.h>
#include <chiaki/remote/holepunch.h>
#include <chiaki/session.h>
#include <chiaki/time.h>
#include "../../lib/src/utils.h"

#include <QKeyEvent>
#include <cstring>

#define SETSU_UPDATE_INTERVAL_MS 4
#define STEAMDECK_UPDATE_INTERVAL_MS 4

// Dimensões dos touchpads (mantidas para lógica de input)
#define PS4_TOUCHPAD_MAX_X 1920.0f
#define PS4_TOUCHPAD_MAX_Y 942.0f
#define PS5_TOUCHPAD_MAX_X 1919.0f
#define PS5_TOUCHPAD_MAX_Y 1079.0f

static bool isLocalAddress(QString host) {
    if(host.contains(".")) {
        if(host.startsWith("10.") || host.startsWith("192.168.")) return true;
        for (int j = 16; j < 32; j++) {
            if(host.startsWith(QString("172.") + QString::number(j) + QString("."))) return true;
        }
    } else if(host.contains(":")) {
        if(host.startsWith("FC", Qt::CaseInsensitive) || host.startsWith("FD", Qt::CaseInsensitive)) return true;
    }
    return false;
}

StreamSessionConnectInfo::StreamSessionConnectInfo(...) {
    // Inicialização básica de flags e chaves, removendo referências a decoders e áudio
    key_map = settings->GetControllerMappingForDecoding();
    this->target = target;
    this->host = host;
    this->regist_key = regist_key;
    this->morning = morning;
    this->fullscreen = fullscreen;
    this->buttons_by_pos = settings->GetButtonsByPosition();
    this->psn_token = settings->GetPsnAuthToken();
    this->psn_account_id = settings->GetPsnAccountId();
    this->duid = duid;
}

static void EventCb(ChiakiEvent *event, void *user);

StreamSession::StreamSession(const StreamSessionConnectInfo &connect_info, QObject *parent)
	: QObject(parent),
	log(this, connect_info.log_level_mask, connect_info.log_file),
	session_started(false),
	connected(false),
	holepunch_session(nullptr)
{
    // Removido inicialização de Decoders (FFmpeg/Pi)
    // Removido inicialização de Áudio/Opus/Speex

	ChiakiConnectInfo chiaki_connect_info = {};
	chiaki_connect_info.ps5 = chiaki_target_is_ps5(connect_info.target);
	chiaki_connect_info.host = connect_info.host.toUtf8().constData();
    
    // Configurações de touchpad baseadas no alvo
    if(chiaki_connect_info.ps5) {
        PS_TOUCHPAD_MAX_X = PS5_TOUCHPAD_MAX_X;
        PS_TOUCHPAD_MAX_Y = PS5_TOUCHPAD_MAX_Y;
    } else {
        PS_TOUCHPAD_MAX_X = PS4_TOUCHPAD_MAX_X;
        PS_TOUCHPAD_MAX_Y = PS4_TOUCHPAD_MAX_Y;
    }

	chiaki_controller_state_set_idle(&keyboard_state);
	chiaki_controller_state_set_idle(&touch_state);

    // Lógica de Conexão PSN / Holepunch
	if(!connect_info.duid.isEmpty()) {
		InitiatePsnConnection(connect_info.psn_token);
		chiaki_connect_info.holepunch_session = holepunch_session;
        QByteArray psn_acc = QByteArray::fromBase64(connect_info.psn_account_id.toUtf8());
        memcpy(chiaki_connect_info.psn_account_id, psn_acc.constData(), CHIAKI_PSN_ACCOUNT_ID_SIZE);
	}

	chiaki_session_init(&session, &chiaki_connect_info, GetChiakiLog());
	chiaki_session_set_event_cb(&session, EventCb, this);

    // Sinks de vídeo e áudio foram REMOVIDOS aqui
    
    // Inicialização de Gamepads (Apenas Input)
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	connect(ControllerManager::GetInstance(), &ControllerManager::AvailableControllersUpdated, this, &StreamSession::UpdateGamepads);
	UpdateGamepads();
#endif

    // Timer de estatísticas de rede
	QTimer *packet_loss_timer = new QTimer(this);
	connect(packet_loss_timer, &QTimer::timeout, this, [this]() {
		if(packet_loss_history.size() > 10) packet_loss_history.takeFirst();
		packet_loss_history.append(session.stream_connection.congestion_control.packet_loss);
        // ... cálculo de média ...
	});
	packet_loss_timer->start(200);
}

StreamSession::~StreamSession() {
	if(session_started) chiaki_session_join(&session);
	chiaki_session_fini(&session);
    // Removido limpeza de áudio, decoders e resamplers
}

// --- Métodos de Controle ---

void StreamSession::SendFeedbackState() {
	ChiakiControllerState state;
	chiaki_controller_state_set_idle(&state);

#if CHIAKI_GUI_ENABLE_SETSU
    chiaki_controller_state_or(&state, &state, &setsu_state);
#endif

	for(auto controller : controllers) {
		auto controller_state = controller->GetState();
		chiaki_controller_state_or(&state, &state, &controller_state);
	}

	chiaki_controller_state_or(&state, &state, &keyboard_state);
	chiaki_controller_state_or(&state, &state, &touch_state);

	chiaki_session_set_controller_state(&session, &state);
}

void StreamSession::HandleKeyboardEvent(QKeyEvent *event) {
	if(!key_map.contains(Qt::Key(event->key())) || event->isAutoRepeat()) return;

	int button = key_map[Qt::Key(event->key())];
	bool press = event->type() == QEvent::KeyPress;

    // Lógica de mapeamento de botões e analógicos (mantida do original)
    // ... (switch button case) ...

	SendFeedbackState();
}

// --- Eventos de Conexão ---

void StreamSession::Event(ChiakiEvent *event) {
	switch(event->type) {
		case CHIAKI_EVENT_CONNECTED:
			connected = true;
			emit ConnectedChanged();
			break;
		case CHIAKI_EVENT_QUIT:
			connected = false;
			emit SessionQuit(event->quit.reason, QString::fromUtf8(event->quit.reason_str));
			break;
		case CHIAKI_EVENT_RUMBLE: {
            // Repassa o rumble para os controles físicos, mesmo sem áudio
			uint8_t l = event->rumble.left, r = event->rumble.right;
			for(auto c : controllers) c->SetRumble(l, r);
			break;
		}
		default: break;
	}
}

// Funções de callback estáticas simplificadas
static void EventCb(ChiakiEvent *event, void *user) {
	auto s = reinterpret_cast<StreamSession *>(user);
	s->Event(event);
}

void StreamSession::Start() { chiaki_session_start(&session); }
void StreamSession::Stop() { chiaki_session_stop(&session); }
