#include <Player.h>
#include <Packet.h>
#include <Server.h>
#include <Colors.h>
#include <States.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
    typedef int SOCKET;
#endif

// Порт API, который указан в Python скрипте (API_PORT = 5010)
#define MASTER_API_PORT 5010
#define MASTER_API_HOST "127.0.0.1"

// Функция запрашивает у Мастера новый порт для лобби
uint32_t request_next_lobby_port() 
{
    SOCKET sock;
    struct sockaddr_in server_addr;
    char send_buf[256];
    char recv_buf[512];
    int bytes_received;
    uint32_t target_port = 0;

    // Инициализация Winsock для Windows
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 0;
    #endif

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 0;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(MASTER_API_PORT);
    // Преобразование IP
    if (inet_pton(AF_INET, MASTER_API_HOST, &server_addr.sin_addr) <= 0) {
        closesocket(sock);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 0;
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        closesocket(sock);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 0;
    }

    // Формируем простой HTTP GET запрос
    snprintf(send_buf, sizeof(send_buf), 
        "GET /find_lobby HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n\r\n", 
        MASTER_API_HOST);

    send(sock, send_buf, (int)strlen(send_buf), 0);

    // Читаем ответ
    bytes_received = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
    if (bytes_received > 0) {
        recv_buf[bytes_received] = '\0';
        
        // Ищем конец заголовков HTTP (двойной перенос строки)
        char* body = strstr(recv_buf, "\r\n\r\n");
        if (body) {
            body += 4; // Пропускаем \r\n\r\n
            target_port = (uint32_t)atoi(body);
        } else {
            // Если заголовков нет (странно, но попробуем парсить всё)
            target_port = (uint32_t)atoi(recv_buf); 
        }
    }

    closesocket(sock);
    #ifdef _WIN32
        WSACleanup();
    #endif

    return target_port;
}

#define PLRSTATE_ESCAPED 4
#define PLRSTATE_ALIVE 3
#define PLRSTATE_DEAD 2
#define PLRSTATE_DEMONIZED 1
#define MASTER_SERVER_PORT 8606 
#define PLRSTATE_EXE 0

const char* SHAMES_1[] = 
{
	CLRCODE_RED "(my skill issue makes me allergic to moving)",
	CLRCODE_RED "(i'm afraid to leave my camp spot)",
	CLRCODE_RED "(how am i not bored of camping)",
	CLRCODE_RED "(i'm too bad to move around the map)",
	CLRCODE_RED "(i just like being a brick)"
};
#define SHAMES1_CNT 5

const char* SHAMES_2[] = 
{
	CLRCODE_RED "(i can't win without camping bodies)",
	CLRCODE_RED "(i'm afraid revived players will make me lose)",
	CLRCODE_RED "(i camp bodies cuz im bad)"
};
#define SHAMES2_CNT 3

int compare(const PeerData** plr1, const PeerData** plr2)
{
	const PeerData* a = *plr1;
	const PeerData* b = *plr2;

	if (a->plr.flags & PLAYER_KILLER || b->plr.flags & PLAYER_KILLER)
		return ((b->plr.flags & PLAYER_KILLER) > 0) - ((a->plr.flags & PLAYER_KILLER) > 0);

	if (a->plr.flags & PLAYER_LEFT || b->plr.flags & PLAYER_LEFT)
		return ((a->plr.flags & PLAYER_LEFT) > 0) - ((b->plr.flags & PLAYER_LEFT) > 0);

	if (a->plr.flags & PLAYER_DEMONIZED || b->plr.flags & PLAYER_DEMONIZED)
		return ((a->plr.flags & PLAYER_DEMONIZED) > 0) - ((b->plr.flags & PLAYER_DEMONIZED) > 0);

	if (a->plr.flags & PLAYER_DEAD || b->plr.flags & PLAYER_DEAD)
		return ((a->plr.flags & PLAYER_DEAD) > 0) - ((b->plr.flags & PLAYER_DEAD) > 0);

	return 0;
}

int compare2(const PeerData** plr1, const PeerData** plr2)
{
	const PeerData* a = *plr1;
	const PeerData* b = *plr2;

	if (a->plr.flags & PLAYER_KILLER || b->plr.flags & PLAYER_KILLER)
		return ((b->plr.flags & PLAYER_KILLER) > 0) - ((a->plr.flags & PLAYER_KILLER) > 0);

	if (a->plr.flags & PLAYER_LEFT || b->plr.flags & PLAYER_LEFT)
		return 0;

	if (a->plr.flags & PLAYER_DEMONIZED || b->plr.flags & PLAYER_DEMONIZED)
		return 0;

	if ((a->plr.flags & PLAYER_DEAD) > 0 == (b->plr.flags & PLAYER_DEAD) > 0)
		return (int)(b->plr.stats.danger_time - a->plr.stats.danger_time);

	return 0;
}

bool results_send(Server* server, PeerData* v, PeerData* data, bool has_quit)
{
	uint8_t type = PLRSTATE_ALIVE;

	if (data->plr.flags & PLAYER_ESCAPED)
		type = PLRSTATE_ESCAPED;
	if (data->plr.flags & PLAYER_DEMONIZED)
		type = PLRSTATE_DEMONIZED;
	else if (data->plr.flags & PLAYER_DEAD)
		type = PLRSTATE_DEAD;
	else if (server->game.exe == data->id)
		type = PLRSTATE_EXE;

	Packet pack;
	PacketCreate(&pack, SERVER_RESULTS_DATA);

	String nickname;
	const char* postfix = "";

	if (g_config.states.results_misc.pride)
	{
		if (data->plr.stats.brain_damage && (data->plr.flags & PLAYER_ESCAPED))
			postfix = SHAMES_1[rand() % SHAMES1_CNT];

		if (data->plr.stats.camp_time >= 30 * TICKSPERSEC)
			postfix = SHAMES_2[rand() % SHAMES2_CNT];
	}

	nickname.len = snprintf(nickname.value, 129, "%s %s", data->nickname.value, postfix) + 1;

    PacketWrite(&pack, packet_writestr, g_config.states.lobby_misc.anonymous_mode ? string_new("anonymous") : nickname);
	PacketWrite(&pack, packet_write8, 	data->exe_char != EX_NONE ? data->exe_char : data->surv_char);
	PacketWrite(&pack, packet_write8,	server->game.ending);
	PacketWrite(&pack, packet_write16,	server->game.time_sec)
	PacketWrite(&pack, packet_write8,	has_quit);
	PacketWrite(&pack, packet_write8,	type);

	PacketWrite(&pack, packet_write16,		data->plr.stats.rings);
	PacketWrite(&pack, packet_write16,		data->plr.stats.kills);
	PacketWrite(&pack, packet_write16,		data->plr.stats.damage);
	PacketWrite(&pack, packet_write16,		data->plr.stats.damage_taken);
	PacketWrite(&pack, packet_write16,		data->plr.stats.stun_time);
	PacketWrite(&pack, packet_write16,		data->plr.stats.stuns);
	PacketWrite(&pack, packet_write16,		data->plr.stats.hp_restored);
	PacketWrite(&pack, packet_writedouble,	data->plr.stats.survive_time);
	PacketWrite(&pack, packet_writedouble,	data->plr.stats.danger_time);

	return packet_send(v->peer, &pack, true);
}

bool results_init(Server* server)
{
	Debug("Attepting to enter ST_RESULTS...");
	server->state = ST_RESULTS;
    server->results.countdown = g_config.states.results_misc.timer * TICKSPERSEC;

	Packet pack;
	PacketCreate(&pack, SERVER_RESULTS);
	PacketWrite(&pack, packet_write8, server->game.map);
	server_broadcast(server, &pack, true);

	Info("Server is now in " LOG_PUR "Results");
	return true;
}

bool results_uninit(Server* server)
{
    // 1. Очистка сущностей
    for (size_t i = 0; i < server->game.entities.capacity; i++)
    {
        Entity* entity = (Entity*)server->game.entities.ptr[i];
        if (!entity) continue;
        free(entity);
    }
    dylist_free(&server->game.entities);

    // 2. Очистка списка вышедших игроков
    for (size_t i = 0; i < server->game.left.capacity; i++)
    {
        PeerData* player = (PeerData*)server->game.left.ptr[i];
        if (!player) continue;
        free(player);
    }
    dylist_free(&server->game.left);

    // 3. --- ПЕРЕХОД В НОВОЕ ЛОББИ ---

    // Сбрасываем состояние сервера в лобби (чтобы клиенты увидели меню, пока ждут)
    lobby_init(server);
    if (server->host) enet_host_flush(server->host);

    #ifdef _WIN32
        Sleep(500); // Небольшая пауза
    #else
        usleep(500000);
    #endif

    // ЗАПРОС К МАСТЕР-СЕРВЕРУ ЧЕРЕЗ API
    Info("Requesting new lobby port from Master Server...");
    uint32_t next_lobby_port = request_next_lobby_port();

    if (next_lobby_port == 0) {
        Warn("Failed to get lobby from API! Fallback to Master Port: %d", MASTER_SERVER_PORT);
        next_lobby_port = MASTER_SERVER_PORT;
    } else {
        Info("Master Server assigned next lobby: %d", next_lobby_port);
    }

    // Создаем пакет перенаправления
    Packet pack;
    PacketCreate(&pack, SERVER_LOBBY_CHANGELOBBY);
    PacketWrite(&pack, packet_write32, next_lobby_port); 
    
    // Рассылаем всем
    server_broadcast(server, &pack, true);

    // Флашим пакеты
    if (server->host) {
        enet_host_flush(server->host);
    }

    // Ждем, чтобы пакеты точно ушли
    #ifdef _WIN32
        Sleep(1000); // 1 секунда на отправку и обработку клиентом
    #else
        usleep(1000000);
    #endif

    // Выставляем флаг остановки текущего процесса сервера
    server->running = false; 

    return true; 
}

bool results_state_tick(Server* server)
{
	server->results.countdown -= server->delta;
	
	if (server->results.countdown <= 0)
		return results_uninit(server);

	return true;
}

bool results_state_handle(PeerData* v, Packet* packet)
{
	PacketRead(_passtrough, packet, packet_read8, uint8_t);
	PacketRead(type, packet, packet_read8, uint8_t);

	switch (type) 
	{
		case CLIENT_RESULTS_REQUEST:
		{
			PeerData** sort = malloc(sizeof(PeerData*) * g_config.server_config.pairing.maximum_players_per_lobby);
			if (!sort) {
				Err("Allocation failure");
				free(sort);
				return false;
			}

			int len = 0;
            memset(sort, 0, sizeof(PeerData*) * g_config.server_config.pairing.maximum_players_per_lobby);

			for (size_t i = 0; i < v->server->peers.capacity; i++)
			{
				PeerData* data = (PeerData*)v->server->peers.ptr[i];
				if (!data)
					continue;

				if(!data->in_game)
					continue;

				sort[len++] = data;
			}

			for (size_t i = 0; i < v->server->game.left.capacity; i++)
			{
				PeerData* data = (PeerData*)v->server->game.left.ptr[i];
				if (!data)
					continue;

				if(!data->in_game)
					continue;

				sort[len++] = data;
			}

			qsort(sort, len, sizeof(PeerData*), (int (*)(const void *, const void *))compare);
			qsort(sort, len, sizeof(PeerData*), (int (*)(const void *, const void *))compare2);

			for (size_t i = 0; i < len; i++)
			{
				PeerData* data = (PeerData*)sort[i];
				if (!data)
					continue;

				RAssert(results_send(v->server, v, data, data->plr.flags & PLAYER_LEFT));
			}
			free(sort);

			break;
		}
		
		case CLIENT_CHAT_MESSAGE:
		{
			if (v->in_game)
				break;

			PacketRead(pid, packet, packet_read16, uint16_t);
			PacketRead(msg, packet, packet_readstr, String);
			AssertOrDisconnect(v->server, string_length(&msg) <= 40);

			v->timeout = 0;

			Info("[%s] (id %d): %s", v->nickname.value, v->id, msg.value);
            if (!server_cmd_handle(v->server, server_cmd_parse(&msg), v, &msg) && g_config.states.lobby_misc.apply_textchat_fixes)
                server_broadcast_msg(v->server, v->id, msg.value);
			break;
		}
	}

	return true;
}
