#include "music_app.h"
#include "wifi_config.h"
#include "audio_out.h"
#include <M5Unified.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------
// AP + audio lifecycle
// ---------------------------------------------------------------------------
void MusicApp::enter() {
  if (_started) return;

  // Host our own Wi-Fi network for the phone to join.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(MUSIC_AP_SSID, MUSIC_AP_PASS);

  // Bring up the external I2S DAC and prove the line-out path with a tone.
  audioOutBegin(44100);
  testTone();

  _started     = true;
  _lastClients = -1;
}

void MusicApp::leave() {
  if (!_started) return;
  audioOutEnd();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  _started = false;
}

void MusicApp::testTone() {
  audioOutTestTone(880, 250);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------
void MusicApp::draw() {
  enter();   // first draw brings the AP up

  auto& d = M5.Display;
  int w = d.width(), h = d.height();
  d.fillScreen(TFT_BLACK);

  // Title bar (reuse the PAYLOADS purple)
  d.fillRect(0, 0, w, 28, 0xAA00FF);
  d.setTextDatum(MC_DATUM);
  d.setTextColor(TFT_WHITE);
  d.setTextSize(1);
  d.drawString("MUSIC", w / 2, 14);

  int clients = WiFi.softAPgetStationNum();
  _lastClients = clients;

  d.setTextDatum(TL_DATUM);
  int x = 6, y = 40;

  d.setTextColor(0x888888); d.drawString("Wi-Fi:", x, y);
  d.setTextColor(TFT_WHITE); d.drawString(MUSIC_AP_SSID, x, y + 14); y += 38;

  d.setTextColor(0x888888); d.drawString("Pass:", x, y);
  d.setTextColor(TFT_WHITE); d.drawString(MUSIC_AP_PASS, x, y + 14); y += 38;

  d.setTextColor(0x888888); d.drawString("Device IP:", x, y);
  d.setTextColor(TFT_WHITE); d.drawString(WiFi.softAPIP().toString().c_str(), x, y + 14); y += 38;

  // Connection status
  d.setTextColor(0x888888); d.drawString("Phones:", x, y);
  d.setTextColor(clients > 0 ? TFT_GREEN : 0x555555);
  d.drawString(clients > 0 ? "connected" : "waiting...", x, y + 14); y += 34;

  // Footer hint
  d.setTextDatum(MC_DATUM);
  d.setTextSize(1);
  d.setTextColor(0x666666);
  d.drawString("Join Wi-Fi, then AirPlay", w / 2, h - 26);
  d.setTextColor(0x444444);
  d.drawString("A: test tone", w / 2, h - 12);
}

void MusicApp::update() {
  // Poll for client count changes ~1Hz and redraw on change.
  if (millis() - _lastPoll < 1000) return;
  _lastPoll = millis();
  if (WiFi.softAPgetStationNum() != _lastClients) draw();
}

void MusicApp::onBtnB() {
  testTone();
}

void MusicApp::onBtnBHoldAlt() {
  leave();
  Navigator::pop();
}
