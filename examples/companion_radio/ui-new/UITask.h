#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

class UITask : public AbstractUITask {
  // ── Private Member ─────────────────────────────────────────────────────────
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  int _num_unread;               // V5: UI-interner Unread-Zaehler (unabhaengig von _msgcount)
  char _latest_fav_name[33];     // V5: Name des juengsten Favoriten mit Nachricht
  uint32_t _latest_fav_time;     // V5: Zeitstempel seiner letzten Nachricht
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
#ifdef DISP_BACKLIGHT
  bool _backlight_on = false;           // aktueller Backlight-Zustand
  bool _backlight_initialized = false;  // wird erst nach Boot auf true gesetzt
  #ifdef PIN_BUTTON2
  bool _btn2_was_pressed = false;       // Entprellungs-Flag Toggle-Knopf
  #endif
#endif
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  // Screen-Instanzen
  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_history;    // V5: ersetzt msg_preview
  UIScreen* msg_filter;     // V5: Filter/Navigation-Screen
  UIScreen* outdoor_menu;
  UIScreen* sos_alert;      // V3: SOS Alarm-Screen (Empfang)
  UIScreen* sos_send;       // V3: SOS Sende-Screen
#if UI_HAS_JOYSTICK
  UIScreen* channel_select; // V5: Channel-Auswahl (L1 only)
  UIScreen* msg_compose;    // V5: Tastatur-Screen (L1 only)
#endif
  UIScreen* curr;

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

public:
  // ── Public Interface ───────────────────────────────────────────────────────

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
    _num_unread = 0;           // V5
    _latest_fav_name[0] = 0;  // V5
    _latest_fav_time = 0;     // V5
  }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // V5: setCurrScreen public — Screen-Klassen koennen direkt navigieren (E3)
  void setCurrScreen(UIScreen* c);

  // Navigation helpers
  void gotoHomeScreen()    { setCurrScreen(home); }
  void gotoMsgFilter()     { setCurrScreen(msg_filter); }  // V5: fuer MsgComposeScreen::_doSend()
#if UI_HAS_JOYSTICK
  void gotoChannelSelect();  // V5: laedt Channels und wechselt zu channel_select (impl. in .cpp)
#endif

  void showAlert(const char* text, int duration_millis);

  // Getter
  int         getMsgCount()  const { return _msgcount; }
  int         getNumUnread() const { return _num_unread; }                    // V5
  const char* getLatestFavoriteName() const { return _latest_fav_name; }     // V5
  uint32_t    getLatestFavoriteTime() const { return _latest_fav_time; }     // V5
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

  // V5: Synchronisation des _num_unread-Zaehlers durch MsgHistoryScreen (N6)
  void decrementUnread() { if (_num_unread > 0) _num_unread--; }
  void resetUnread()     { _num_unread = 0; }

  bool isBuzzerQuiet() {
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();

  void triggerSOS(const char* from, const char* text);  // V3
  bool isBuzzerPlaying();                               // V3
  void playSOSAlarm();                                  // V3
  void stopBuzzer();                                    // V3

  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, bool is_favorite = false) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
  void refreshDisplay() override;
  bool isOnRecentOrTrackingPage() const override;
};
