#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <stddef.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#include "Button.h"

#ifdef HAS_DRV2605
  #include <helpers/ui/DRV2605Vibration.h>
#endif

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef HAS_DRV2605
  DRV2605Vibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _version_info[32];
  char _origin[62];
  char _msg[80];
  char _alert[80];
  int _msgcount;
  bool _need_refresh = true;
  bool _displayWasOn = false;  // Track display state before button press
  bool _sos_active = false;            // V5: SOS alarm active (siren loops until short press)
  bool _buzzer_restore_quiet = false;  // V5: restore quiet state after forced tone playback
  bool _sos_holding = false;           // v6.2: click + hold SOS countdown running
  unsigned long _sos_hold_start = 0;   // v6.2: countdown start
  unsigned long _sos_last_beep = 0;    // v6.2: last countdown beep (LED blinks with it)
  unsigned long _sos_next_beep = 0;    // v6.2: next countdown beep
  unsigned long ui_started_at;

  // Button handlers
#ifdef PIN_USER_BTN
  Button* _userButton = nullptr;
#endif
#ifdef PIN_USER_BTN_ANA
  Button* _userButtonAnalog = nullptr;
#endif

  void renderCurrScreen();
  void userLedHandler();
  void renderBatteryIndicator(uint16_t batteryMilliVolts);
  
  // Button action handlers
  void handleButtonAnyPress();
  void handleButtonShortPress();
  void handleButtonDoublePress();
  void handleButtonTriplePress();
  void handleButtonQuadruplePress();
  void handleButtonQuintuplePress();  // V2.02: Off-Grid Toggle
  void handleButtonLongPress();
  void handleButtonClickHoldStart();  // v6.2: click + hold = SOS countdown
  void handleButtonClickHoldEnd();    // v6.2: released before the end = cancel
  void sosCountdownLoop();            // v6.2
  void playForced(const char* melody);  // v6.2: tone even while the buzzer is quiet

 
public:

  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
      _next_refresh = 0;
      ui_started_at = 0;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  bool hasDisplay() const { return _display != NULL; }
  void clearMsgPreview();

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int offline_queue_size) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;

  // from AbstractUITask
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;
  void triggerSOS(const char* from, const char* text);  // V5: SOS alarm via buzzer

  void shutdown(bool restart = false);
};
