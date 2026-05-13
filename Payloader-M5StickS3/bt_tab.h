#pragma once
#include "menu_screen.h"
#include <BLEServer.h>

extern MenuTab btMenuTab;
extern bool    g_btActive;
extern bool    g_btConnected;
extern char    g_btPeerName[32];
extern char    g_btPeerAddr[18];

void       btTabSetup();   // Call after M5.begin() — inits BLE device + server
void       btTabUpdate();
BLEServer* btGetServer();  // Returns the shared BLE server (used by fido_bt)
