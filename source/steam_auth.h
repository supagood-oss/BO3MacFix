#ifndef BO3_STEAM_AUTH_H
#define BO3_STEAM_AUTH_H
#include <stdbool.h>
#include "steam/ISteamUser.h"
void steam_http_auth_context(ISteamUser **user);
void steam_http_auth_begin(bool enabled);
char *steam_http_ticket_copy(void);
#endif
