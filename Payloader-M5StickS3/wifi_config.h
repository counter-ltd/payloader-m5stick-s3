#pragma once

// ---------------------------------------------------------------------------
// Music payload — SoftAP credentials
// ---------------------------------------------------------------------------
// The Music payload makes the M5Stick host its OWN Wi-Fi network. Join this
// network from your iPhone, then pick the device from the AirPlay list.
//
// Password must be 8-63 chars (WPA2). Edit before flashing if you want to
// change them.
#define MUSIC_AP_SSID  "M5 Music"
#define MUSIC_AP_PASS  "SnowWhitehouse"

// mDNS / AirPlay service name the device advertises (Stage 2+).
#define MUSIC_AP_HOSTNAME  "M5-Music"
