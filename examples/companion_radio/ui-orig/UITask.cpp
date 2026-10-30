#include "UITask.h"
#include <Arduino.h>
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"

#define AUTO_OFF_MILLIS     15000   // 15 seconds
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#if defined(PIN_STATUS_LED) || defined(PIN_STATUS_LED_R)
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

// v6.2: single LED (T1000-E), RECHERCHE_LED_Buzzer_Headless.md section 7
#define LED_BOOT_MILLIS       2000   // LED on after start
#define LED_SOS_BLINK_MILLIS   100   // received SOS: fast blink (on/off each 100 ms)

// v6.2: tracking "breath" (single LED, nRF52), played by the PWM hardware from a table (see
// ledBreathStart()). Slow and with only a short pause between breaths (Tobias, 30.10.2026)
#define LED_BREATH_MILLIS      4000   // one breath, in + out
#define LED_BREATH_PAUSE_MILLIS 300   // LED off between two breaths
#define LED_BREATH_STEP_MILLIS   20   // brightness changes every 20 ms
#define LED_BREATH_PEAK         100   // peak brightness in %
#define LED_BREATH_GAMMA       2.2f   // perceived brightness -> PWM duty (eye is not linear)

#if defined(PIN_STATUS_LED_R) && defined(PIN_STATUS_LED_G) && defined(PIN_STATUS_LED_B)
#define STATUS_LED_RGB 1
#ifndef LOW_BATT_MILLIVOLTS
#define LOW_BATT_MILLIVOLTS 3500
#endif
#endif

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

// v6.2: SOS by click + hold (RECHERCHE_LED_Buzzer_Headless.md 7a)
#define SOS_HOLD_MILLIS        5000   // hold this long to send
#define SOS_BEEP_FIRST_MILLIS  1000   // countdown beep interval at the start ...
#define SOS_BEEP_LAST_MILLIS    150   // ... and at the end
#define SOS_LED_ON_MILLIS        60   // LED on with each countdown beep

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  clearMsgPreview();
  _node_prefs = node_prefs;
  if (_display != NULL) {
    _display->turnOn();
  }

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(FIRMWARE_VERSION);
  char *dash = strchr(version, '-');
  if (dash) {
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  sprintf(_version_info, "%s (%s)", version, FIRMWARE_BUILD_DATE);

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef HAS_DRV2605
  vibration.begin();
  vibration.quiet(_node_prefs->vibe_quiet);
#endif

  // Initialize digital button if available
#ifdef PIN_USER_BTN
  _userButton = new Button(PIN_USER_BTN, USER_BTN_PRESSED);
  _userButton->begin();
  
  // Set up digital button callbacks
  _userButton->onShortPress([this]() { handleButtonShortPress(); });
  _userButton->onDoublePress([this]() { handleButtonDoublePress(); });
  _userButton->onTriplePress([this]() { handleButtonTriplePress(); });
  _userButton->onQuadruplePress([this]() { handleButtonQuadruplePress(); });
  _userButton->onQuintuplePress([this]() { handleButtonQuintuplePress(); });  // V2.02
  _userButton->onLongPress([this]() { handleButtonLongPress(); });
  _userButton->onAnyPress([this]() { handleButtonAnyPress(); });
  _userButton->onClickHoldStart([this]() { handleButtonClickHoldStart(); });   // v6.2
  _userButton->onClickHoldEnd([this]() { handleButtonClickHoldEnd(); });       // v6.2
#endif

  // Initialize analog button if available
#ifdef PIN_USER_BTN_ANA
  _userButtonAnalog = new Button(PIN_USER_BTN_ANA, USER_BTN_PRESSED, true, 20);
  _userButtonAnalog->begin();
  
  // Set up analog button callbacks
  _userButtonAnalog->onShortPress([this]() { handleButtonShortPress(); });
  _userButtonAnalog->onDoublePress([this]() { handleButtonDoublePress(); });
  _userButtonAnalog->onTriplePress([this]() { handleButtonTriplePress(); });
  _userButtonAnalog->onQuadruplePress([this]() { handleButtonQuadruplePress(); });
  _userButtonAnalog->onQuintuplePress([this]() { handleButtonQuintuplePress(); });  // V2.02
  _userButtonAnalog->onLongPress([this]() { handleButtonLongPress(); });
  _userButtonAnalog->onAnyPress([this]() { handleButtonAnyPress(); });
  _userButtonAnalog->onClickHoldStart([this]() { handleButtonClickHoldStart(); });   // v6.2
  _userButtonAnalog->onClickHoldEnd([this]() { handleButtonClickHoldEnd(); });       // v6.2
#endif
  ui_started_at = millis();
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif
//  Serial.print("DBG:  Alert user -> ");
//  Serial.println((int) t);
}

void UITask::onQueueSizeChanged(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    clearMsgPreview();
  }
}

void UITask::clearMsgPreview() {
  _origin[0] = 0;
  _msg[0] = 0;
  _need_refresh = true;
}

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  // we only want to show text messages on display, not cli data
  if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;

#ifdef HAS_DRV2605
  vibration.trigger();   // vibrate even while the app is connected (honors quiet + cooldown)
#endif

  uint8_t path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  if (path_len == 0xFF) {
    sprintf(_origin, "(F) %s", from.name);
  } else {
    sprintf(_origin, "(%d) %s", (uint32_t) path_len, from.name);
  }
  StrHelper::strncpy(_msg, text, sizeof(_msg));

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
      _need_refresh = true;
    }
  }
  if (!hasConnection()) {
    notify(UIEventType::contactMessage);
  }
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  // V5: SOS-Erkennung — "!SOS" irgendwo im Text (MeshCore stellt Node-Name voran)
  // V6: moved here from MyMesh::onChannelMessageRecv() (listener architecture)
  // v6.1: only in the SOS channel (where sendSOS() sends) - "!SOS" in Public etc. is a normal message
  if (strstr(text, "!SOS") != NULL && MyMesh::isSosChannel(channel_details.name)) {
    triggerSOS(channel_details.name, text);
    return;
  }

#ifdef HAS_DRV2605
  vibration.trigger();   // vibrate even while the app is connected (honors quiet + cooldown)
#endif

  uint8_t path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  if (path_len == 0xFF) {
    sprintf(_origin, "(F) %s", channel_details.name);
  } else {
    sprintf(_origin, "(%d) %s", (uint32_t) path_len, channel_details.name);
  }
  StrHelper::strncpy(_msg, text, sizeof(_msg));

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
      _need_refresh = true;
    }
  }
  if (!hasConnection()) {
    // V5: favourites get the distinct (contact message) tone
    notify(the_mesh.isSenderFavorite(text) ? UIEventType::contactMessage : UIEventType::channelMessage);
  }
}

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (!hasConnection()) {
    notify(UIEventType::newContactMessage);
  }
}

void UITask::renderBatteryIndicator(uint16_t batteryMilliVolts) {
  // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
  const int minMilliVolts = BATT_MIN_MILLIVOLTS;
  const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
  int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
  if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
  if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

  // battery icon
  int iconWidth = 24;
  int iconHeight = 12;
  int iconX = _display->width() - iconWidth - 5; // Position the icon near the top-right corner
  int iconY = 0;
  _display->setColor(UIColor::primary_txt);

  // battery outline
  _display->drawRect(iconX, iconY, iconWidth, iconHeight);

  // battery "cap"
  _display->fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

  // fill the battery based on the percentage
  int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
  _display->fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);
}

void UITask::renderCurrScreen() {
  if (_display == NULL) return;  // assert() ??

  char tmp[80];
  if (_alert[0]) {
    _display->setTextSize(1.4);
    uint16_t textWidth = _display->getTextWidth(_alert);
    _display->setCursor((_display->width() - textWidth) / 2, 22);
    _display->setColor(UIColor::warning_txt);
    _display->print(_alert);
    _alert[0] = 0;
    _need_refresh = true;
    return;
  } else if (_origin[0] && _msg[0]) { // message preview
    // render message preview
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(UIColor::primary_txt);
    _display->print(_node_prefs->node_name);

    _display->setCursor(0, 12);
    _display->setColor(UIColor::secondary_txt);
    _display->print(_origin);
    _display->setCursor(0, 24);
    _display->print(_msg);

    _display->setCursor(_display->width() - 28, 9);
    _display->setTextSize(2);
    _display->setColor(UIColor::primary_txt);
    sprintf(tmp, "%d", _msgcount);
    _display->print(tmp);
    _display->setColor(UIColor::secondary_txt); // last color will be kept on T114
  } else if ((millis() - ui_started_at) < BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    uint16_t textWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - textWidth) / 2, 22);
    _display->print(_version_info);
  } else {  // home screen
    // node name
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(UIColor::primary_txt);
    _display->print(_node_prefs->node_name);

    // battery voltage
    renderBatteryIndicator(_board->getBattMilliVolts());

    // freq / sf
    _display->setCursor(0, 20);
    _display->setColor(UIColor::secondary_txt);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);

    // BT pin
    if (!hasConnection() && the_mesh.getBLEPin() != 0) {
      _display->setColor(UIColor::warning_txt);
      _display->setTextSize(2);
      _display->setCursor(0, 43);
      sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
      _display->print(tmp);
      _display->setColor(UIColor::primary_txt);
    } else {
      _display->setColor(UIColor::primary_txt); 
    }
  }
  _need_refresh = false;
}

#ifdef STATUS_LED_RGB
static void statusLedWrite(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(PIN_STATUS_LED_R, r);
  analogWrite(PIN_STATUS_LED_G, g);
  analogWrite(PIN_STATUS_LED_B, b);
}

static void statusLedWheel(uint8_t pos) {   // smooth hue sweep, 0..255
  if (pos < 85) {
    statusLedWrite(255 - pos * 3, pos * 3, 0);
  } else if (pos < 170) {
    pos -= 85;
    statusLedWrite(0, 255 - pos * 3, pos * 3);
  } else {
    pos -= 170;
    statusLedWrite(pos * 3, 0, 255 - pos * 3);
  }
}
#endif

#if defined(PIN_STATUS_LED) && !defined(STATUS_LED_RGB) && defined(NRF52_PLATFORM)
#define LED_BREATH_HW 1
// v6.2: the tracking breath is played by a PWM module straight from a table in RAM (EasyDMA,
// one value per LED_BREATH_STEP_MILLIS) - the CPU only starts it. The first version wrote
// the PWM from the loop (analogWrite() on every pass, each one restarting the PWM sequence)
// and depended on the loop waking up in time; on the T1000-E it looked uneven and was now and
// then broken by a short bright flash (30.10.2026, cause not pinned down). Now nothing in the
// firmware touches the LED during a breath.
// The module is taken with its own owner token only for the breath and handed back
// afterwards: a running PWM module keeps the 16 MHz clock on (checked on the T1000-E,
// 29.10.2026). PWM2 is never used - tone() (buzzer) owns it.
#define LED_BREATH_TOP     2000   // PWM counter top: 4 MHz / 2000 = 2 kHz, 2000 brightness levels
#define LED_BREATH_STEPS   (LED_BREATH_MILLIS / LED_BREATH_STEP_MILLIS)
#define LED_BREATH_REFRESH (LED_BREATH_STEP_MILLIS * 2 - 1)   // extra PWM periods per value (2 kHz)

static const uint32_t LED_BREATH_TOKEN = 0x6c446d46;   // PWM owner token ('FmDl')
static uint16_t led_breath_seq[LED_BREATH_STEPS];      // in RAM: EasyDMA cannot read flash
static int led_breath_mod = -1;                        // PWM module in use, -1 = none

static NRF_PWM_Type* ledPwmModule(int i) {   // same order as HwPWMx[]
  switch (i) {
    case 0: return NRF_PWM0;
    case 1: return NRF_PWM1;
#ifdef NRF_PWM3
    case 3: return NRF_PWM3;
#endif
    default: return NULL;   // PWM2: tone()
  }
}

static void ledBreathTable() {   // eased in and out, then gamma corrected - computed once
  static bool done = false;
  if (done) return;
  done = true;
  const uint16_t polarity = LED_STATE_ON ? 0x8000 : 0;   // bit 15 set: output ON for 'value' counts
  for (int i = 0; i < LED_BREATH_STEPS; i++) {
    float s = (1.0f - cosf(2.0f * (float)PI * i / (LED_BREATH_STEPS - 1))) * 0.5f;   // 0 -> 1 -> 0
    float duty = powf(s, LED_BREATH_GAMMA) * LED_BREATH_PEAK / 100.0f;
    led_breath_seq[i] = (uint16_t)(duty * LED_BREATH_TOP + 0.5f) | polarity;
  }
}

static bool ledBreathStart() {
  ledBreathTable();
  for (int i = 0; i < HWPWM_MODULE_NUM; i++) {
    NRF_PWM_Type* p = ledPwmModule(i);
    if (p == NULL || !HwPWMx[i]->takeOwnership(LED_BREATH_TOKEN)) continue;
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, !LED_STATE_ON);   // pin level whenever the module is not running
    p->PSEL.OUT[0] = g_ADigitalPinMap[PIN_STATUS_LED];
    p->PSEL.OUT[1] = 0xFFFFFFFFUL;
    p->PSEL.OUT[2] = 0xFFFFFFFFUL;
    p->PSEL.OUT[3] = 0xFFFFFFFFUL;
    p->MODE = PWM_MODE_UPDOWN_Up;
    p->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_4;   // 4 MHz
    p->COUNTERTOP = LED_BREATH_TOP;
    p->DECODER = (PWM_DECODER_LOAD_Common << PWM_DECODER_LOAD_Pos) |
                 (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
    p->LOOP = 0;
    p->SEQ[0].PTR = (uint32_t)led_breath_seq;
    p->SEQ[0].CNT = LED_BREATH_STEPS;
    p->SEQ[0].REFRESH = LED_BREATH_REFRESH;
    p->SEQ[0].ENDDELAY = 0;
    p->SEQ[1].PTR = (uint32_t)led_breath_seq;
    p->SEQ[1].CNT = 0;
    p->SEQ[1].REFRESH = 0;
    p->SEQ[1].ENDDELAY = 0;
    p->INTENCLR = 0xFFFFFFFFUL;
    p->SHORTS = PWM_SHORTS_SEQEND0_STOP_Msk;   // stops by itself at the end, last value is 0
    p->EVENTS_STOPPED = 0;
    p->EVENTS_SEQEND[0] = 0;
    p->ENABLE = 1;
    p->TASKS_SEQSTART[0] = 1;
    led_breath_mod = i;
    return true;
  }
  return false;   // no free PWM module: skip this breath
}

static bool ledBreathRunning() {
  return led_breath_mod >= 0 && !ledPwmModule(led_breath_mod)->EVENTS_STOPPED;
}

static void ledPwmRelease() {   // stop a breath (finished or not), hand the module back, LED off
  if (led_breath_mod < 0) return;
  NRF_PWM_Type* p = ledPwmModule(led_breath_mod);
  if (!p->EVENTS_STOPPED) {
    p->TASKS_STOP = 1;   // takes effect at the end of the current PWM period (0.5 ms)
    uint32_t t0 = micros();
    while (!p->EVENTS_STOPPED && micros() - t0 < 2000) {}
  }
  p->SHORTS = 0;
  p->ENABLE = 0;
  p->PSEL.OUT[0] = 0xFFFFFFFFUL;
  HwPWMx[led_breath_mod]->releaseOwnership(LED_BREATH_TOKEN);
  led_breath_mod = -1;
  digitalWrite(PIN_STATUS_LED, !LED_STATE_ON);
}
#elif defined(PIN_STATUS_LED) && !defined(STATUS_LED_RGB)
static void ledPwmRelease() { }   // no hardware breath on this platform: tracking shows the flash
#endif

void UITask::userLedHandler() {
  // v6.2: SOS countdown - LED blinks with each beep (T1000-E green, X1 red)
  static bool countdown_led = false;
  if (_sos_holding) {
    bool on = (millis() - _sos_last_beep) < SOS_LED_ON_MILLIS;
#ifdef STATUS_LED_RGB
    analogWrite(PIN_STATUS_LED_R, on ? 255 : 0);
    analogWrite(PIN_STATUS_LED_G, 0);
    analogWrite(PIN_STATUS_LED_B, 0);
#elif defined(PIN_STATUS_LED)
    ledPwmRelease();
    digitalWrite(PIN_STATUS_LED, on ? LED_STATE_ON : !LED_STATE_ON);
#endif
    countdown_led = true;
    return;
  }
  if (countdown_led) {   // countdown over: LED off, normal pattern continues
    countdown_led = false;
#ifdef STATUS_LED_RGB
    analogWrite(PIN_STATUS_LED_R, 0);
    analogWrite(PIN_STATUS_LED_G, 0);
    analogWrite(PIN_STATUS_LED_B, 0);
#elif defined(PIN_STATUS_LED)
    digitalWrite(PIN_STATUS_LED, !LED_STATE_ON);
#endif
  }

#ifdef STATUS_LED_RGB
  static bool booted = false;
  static bool flourish_active = false;
  static unsigned long flourish_until = 0;
  static int prev_msgcount = 0;
  static int state = 0;
  static unsigned long next_change = 0;
  static int last_increment = 0;
  static unsigned long next_batt_check = 0;
  static bool low_batt = false;

  unsigned long cur_time = millis();

  if (!booted) {   // color sweep on boot
    booted = true;
    flourish_until = cur_time + 1200;
    flourish_active = true;
  }
  if (_msgcount > prev_msgcount) {   // color sweep when a new message arrives
    flourish_until = cur_time + 800;
    flourish_active = true;
  }
  prev_msgcount = _msgcount;

  if (flourish_active) {
    if (cur_time < flourish_until) {
      statusLedWheel((cur_time % 1200) * 255 / 1200);
      return;
    }
    statusLedWrite(0, 0, 0);
    flourish_active = false;
    state = 0;
    next_change = cur_time;
  }

  if (cur_time > next_batt_check) {   // battery reads are not free, keep them rare
    low_batt = _board->getBattMilliVolts() < LOW_BATT_MILLIVOLTS;
    next_batt_check = cur_time + 60000;
  }

#ifdef EXT_CHRG_DETECT
  // charge status display while docked (skipped when messages are waiting,
  // so the unread indication is never masked)
  static unsigned long next_pwr_check = 0;
  static bool ext_powered = false, ext_charging = false;
  if (cur_time > next_pwr_check) {
    ext_powered = _board->isExternalPowered();
    ext_charging = ext_powered && digitalRead(EXT_CHRG_DETECT) == LOW;
    next_pwr_check = cur_time + 1000;
  }
  if (ext_powered && _msgcount == 0) {
    if (ext_charging) {
      // amber breathing while charging
      int ph = cur_time % 2000;
      int v = ph < 1000 ? ph : 2000 - ph;
      uint8_t lvl = (uint8_t)(v * 255 / 1000);
      statusLedWrite(lvl, (uint8_t)(lvl * 35 / 100), 0);
    } else {
      statusLedWrite(0, 40, 0);   // dim solid green: charge complete
    }
    state = 0;
    next_change = cur_time;
    return;
  }
#endif

  if (cur_time > next_change) {
    if (state == 0) {
      state = 1;
      last_increment = (_msgcount > 0) ? LED_ON_MSG_MILLIS : LED_ON_MILLIS;
      next_change = cur_time + last_increment;
      if (low_batt) {
        statusLedWrite(255, 0, 0);      // red: battery low
      } else if (_msgcount > 0) {
        statusLedWrite(255, 90, 0);     // amber: unread messages
      } else if (hasConnection()) {
        statusLedWrite(0, 0, 255);      // blue: app connected
      } else {
        statusLedWrite(0, 255, 0);      // green: heartbeat
      }
    } else {
      state = 0;
      next_change = cur_time + LED_CYCLE_MILLIS - last_increment;
      statusLedWrite(0, 0, 0);
    }
  }
#elif defined(PIN_STATUS_LED)
  // v6.2 (T1000-E): boot 2 s on, received SOS fast blink, tracking breath, otherwise
  // a short flash every 4 s. Unread messages are no longer shown.
  static bool flash_on = false;
  static unsigned long next_change = 0;
  static bool led_held = false;   // boot light / SOS blink may have left the LED on
  unsigned long now = millis();

  if (now - ui_started_at < LED_BOOT_MILLIS) {
    ledPwmRelease();
    digitalWrite(PIN_STATUS_LED, LED_STATE_ON);
    led_held = true;
    return;
  }
  if (_sos_active) {
    ledPwmRelease();
    digitalWrite(PIN_STATUS_LED, ((now / LED_SOS_BLINK_MILLIS) & 1) ? LED_STATE_ON : !LED_STATE_ON);
    led_held = true;
    return;
  }
  if (led_held) {   // back to normal: LED off, next flash in a second
    led_held = false;
    digitalWrite(PIN_STATUS_LED, !LED_STATE_ON);
    flash_on = false;
    next_change = now + 1000;
  }
#ifdef LED_BREATH_HW
  // tracking = GPS-Share on and the GPS cycle running (same condition as MyMesh::loop()):
  // breath after breath with a short pause, the PWM hardware plays each one on its own
  if (the_mesh.isAutoAdvertEnabled() && the_mesh.gpsCycle().state() != GpsCycle::OFF) {
    static unsigned long next_breath = 0;
    if (flash_on) {   // switched to tracking during a heartbeat flash
      flash_on = false;
      digitalWrite(PIN_STATUS_LED, !LED_STATE_ON);
    }
    if (!ledBreathRunning()) {
      ledPwmRelease();   // previous breath over: module back, 16 MHz clock off
      if ((long)(now - next_breath) >= 0 || (long)(next_breath - now) > LED_BREATH_PAUSE_MILLIS) {
        // due (or stale after a while without tracking: start right away)
        ledBreathStart();
        next_breath = now + LED_BREATH_MILLIS + LED_BREATH_PAUSE_MILLIS;
      }
    }
    return;
  }
#endif

  ledPwmRelease();
  if ((long)(now - next_change) >= 0) {   // late loop -> flash a bit longer, never skipped
    flash_on = !flash_on;
    next_change = now + (flash_on ? LED_ON_MILLIS : LED_CYCLE_MILLIS - LED_ON_MILLIS);
    digitalWrite(PIN_STATUS_LED, flash_on ? LED_STATE_ON : !LED_STATE_ON);
  }
#endif
}

/* 
  hardware-agnostic pre-shutdown activity should be done here 
*/
void UITask::shutdown(bool restart){
#if defined(PIN_STATUS_LED) && !defined(STATUS_LED_RGB)
  ledPwmRelease();   // v6.2: the board may drive the LED pin while powering off
#endif

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    // Power off board including radio, display, GPS and components
    _board->powerOff();
  }
}

void UITask::loop() {
  #ifdef PIN_USER_BTN
    if (_userButton) {
      _userButton->update();
    }
  #endif
  #ifdef PIN_USER_BTN_ANA
    if (_userButtonAnalog) {
      _userButtonAnalog->update();
    }
  #endif
  if (_sos_holding) sosCountdownLoop();   // v6.2
  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) {
    buzzer.loop();
  } else if (!_sos_holding) {   // v6.2: countdown beeps must stay audible
    // V5: restore quiet state after a forced tone has finished playing
    if (_buzzer_restore_quiet) {
      buzzer.quiet(_node_prefs->buzzer_quiet);
      _buzzer_restore_quiet = false;
    }
    // V5: keep SOS siren looping until acknowledged
    if (_sos_active) {
      buzzer.play("siren:d=8,o=5,b=100:d,e,d,e,d,e,d,e");
    }
  }
#endif

  if (_display != NULL && _display->isOn()) {
    static bool _firstBoot = true;
    if(_firstBoot && (millis() - ui_started_at) >= BOOT_SCREEN_MILLIS) {
      _need_refresh = true;
      _firstBoot = false;
    }
    if (millis() >= _next_refresh && _need_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;   // refresh every second
    }
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
#endif
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}

void UITask::handleButtonAnyPress() {
  MESH_DEBUG_PRINTLN("UITask: any press triggered");
  // called on any button press before other events, to wake up the display quickly
  // do not refresh the display here, as it may block the button handler
  if (_display != NULL) {
    _displayWasOn = _display->isOn();  // Track display state before any action
    if (!_displayWasOn) {
      _display->turnOn();
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
  }
}

// ── Button Handlers ───────────────────────────────────────────────────────────

void UITask::handleButtonShortPress() {
  MESH_DEBUG_PRINTLN("UITask: short press triggered");

  // V5: SOS alarm takes priority — short press acknowledges
  if (_sos_active) {
    _sos_active = false;
#ifdef PIN_BUZZER
    buzzer.shutdown();
#endif
    _need_refresh = true;
    return;
  }

  if (_display != NULL) {
    // Only clear message preview if display was already on before button press
    if (_displayWasOn) {
      // If display was on and showing message preview, clear it
      if (_origin[0] && _msg[0]) {
        clearMsgPreview();
      } else {
        // Otherwise, refresh the display
        _need_refresh = true;
      }
    } else {
      _need_refresh = true; // display just turned on, so we need to refresh
    }
    // Note: Display turn-on and auto-off timer extension are handled by handleButtonAnyPress
  }
}

void UITask::handleButtonDoublePress() {
  MESH_DEBUG_PRINTLN("UITask: double press triggered, sending advert");
  if (the_mesh.advert()) {
    MESH_DEBUG_PRINTLN("Advert sent!");
    sprintf(_alert, "Advert sent!");
  } else {
    MESH_DEBUG_PRINTLN("Advert failed!");
    sprintf(_alert, "Advert failed..");
  }
#ifdef PIN_BUZZER
  // double beep — forced regardless of buzzer_quiet
  buzzer.quiet(false);
  buzzer.play("advert:d=16,o=6,b=200:c,p,c");
  _buzzer_restore_quiet = true;
#endif
  _need_refresh = true;
}

void UITask::handleButtonTriplePress() {
  MESH_DEBUG_PRINTLN("UITask: triple press triggered");
  // v6.2: buzzer on/off only, on all devices (the X1 Buzz+Vibe cycle from upstream is gone,
  // vibe_quiet is no longer changed by the button)
#if defined(PIN_BUZZER)
  if (buzzer.isQuiet()) {
    buzzer.quiet(false);
    buzzer.play("buz_on:d=16,o=6,b=200:c,e");
    // no _buzzer_restore_quiet — buzzer stays on
    _node_prefs->buzzer_quiet = 0;
    sprintf(_alert, "Buzzer: ON");
  } else {
    // last tone before going silent
    buzzer.quiet(false);
    buzzer.play("buz_off:d=16,o=6,b=200:e,c");
    _buzzer_restore_quiet = true;  // restore quiet after playback
    _node_prefs->buzzer_quiet = 1;
    sprintf(_alert, "Buzzer: OFF");
  }
  the_mesh.savePrefs();
  _need_refresh = true;
#endif
}

void UITask::handleButtonQuadruplePress() {
  MESH_DEBUG_PRINTLN("UITask: quad press triggered");
  if (_sensors != NULL) {
    // v6.2: GPS and tracking (GPS-Share) together: both on -> both off, otherwise both on.
    // GPS without tracking stays possible via the app
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        // v6.1: decide by the setting (the board reports "0" while the GPS duty cycle sleeps)
        // and store it, so the choice survives a reboot
        if (_node_prefs->gps_enabled && the_mesh.isAutoAdvertEnabled()) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          the_mesh.setGPSAdvertEnabled(false);   // v6.2: tracking off too (saves prefs)
          sprintf(_alert, "GPS: Disabled");
#ifdef PIN_BUZZER
          buzzer.quiet(false);
          buzzer.play("gps_off:d=16,o=6,b=200:e,c");
          _buzzer_restore_quiet = true;
#endif
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          the_mesh.setGPSAdvertEnabled(true);    // v6.2: tracking on too (saves prefs)
          sprintf(_alert, "GPS: Enabled");
#ifdef PIN_BUZZER
          buzzer.quiet(false);
          buzzer.play("gps_on:d=16,o=6,b=200:c,e");
          _buzzer_restore_quiet = true;
#endif
        }
        the_mesh.savePrefs();
        break;
      }
    }
  }
  _need_refresh = true;
}

void UITask::handleButtonQuintuplePress() {
  MESH_DEBUG_PRINTLN("UITask: quintuple press triggered");
  // V2.02: off-grid toggle — buzzer feedback forced regardless of buzzer_quiet
  the_mesh.toggleOffGrid();
#ifdef PIN_BUZZER
  buzzer.quiet(false);
  if (the_mesh.isOffGridActive()) {
    buzzer.play("offgrid_on:d=16,o=6,b=200:c,e");   // ascending = on
  } else {
    buzzer.play("offgrid_off:d=16,o=6,b=200:e,c");  // descending = off
  }
  _buzzer_restore_quiet = true;  // V5: restore quiet state after playback
#endif
  _need_refresh = true;
}

void UITask::handleButtonLongPress() {
  MESH_DEBUG_PRINTLN("UITask: long press triggered");
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
  } else {
    shutdown();
  }
}
// ── SOS ───────────────────────────────────────────────────────────────────────

// v6.2: plays a tone even while the buzzer is quiet (quiet state is restored afterwards)
void UITask::playForced(const char* melody) {
#ifdef PIN_BUZZER
  buzzer.quiet(false);
  buzzer.play(melody);
  _buzzer_restore_quiet = true;
#else
  (void)melody;
#endif
}

// v6.2: click + hold started - countdown to SOS (RECHERCHE_LED_Buzzer_Headless.md 7a).
// A received SOS alarm keeps running (no short press was triggered).
void UITask::handleButtonClickHoldStart() {
  MESH_DEBUG_PRINTLN("UITask: click + hold -> SOS countdown");
  _sos_holding = true;
  _sos_hold_start = millis();
  _sos_next_beep = _sos_hold_start;
  _sos_last_beep = 0;
}

// v6.2: released before the countdown ended - nothing is sent
void UITask::handleButtonClickHoldEnd() {
  if (!_sos_holding) return;   // already sent
  MESH_DEBUG_PRINTLN("UITask: SOS countdown cancelled");
  _sos_holding = false;
  playForced("sos_cancel:d=16,o=5,b=200:e,c");   // short descending tone
}

// v6.2: countdown beeps get faster; after SOS_HOLD_MILLIS the SOS is sent
void UITask::sosCountdownLoop() {
  unsigned long now = millis();
  unsigned long held = now - _sos_hold_start;

  if (held >= SOS_HOLD_MILLIS) {
    _sos_holding = false;
    if (the_mesh.sendSOS()) {
      MESH_DEBUG_PRINTLN("UITask: SOS sent");
      playForced("sos_sent:d=2,o=6,b=100:c");   // one long tone (1.2 s)
#ifdef HAS_DRV2605
      bool q = vibration.isQuiet();   // always vibrate, also when vibration is off
      vibration.quiet(false);
      vibration.trigger(true);
      vibration.quiet(q);
#endif
      sprintf(_alert, "SOS sent!");
    } else {
      MESH_DEBUG_PRINTLN("UITask: SOS failed (no SOS channel?)");
      playForced("sos_fail:d=16,o=4,b=140:c,p,c,p,c");   // three short low tones
      sprintf(_alert, "No SOS channel!");
    }
    _need_refresh = true;
    return;
  }

  if ((long)(now - _sos_next_beep) >= 0) {
    _sos_last_beep = now;
    // interval shrinks linearly from SOS_BEEP_FIRST_MILLIS to SOS_BEEP_LAST_MILLIS
    unsigned long interval = SOS_BEEP_FIRST_MILLIS
        - (held * (SOS_BEEP_FIRST_MILLIS - SOS_BEEP_LAST_MILLIS)) / SOS_HOLD_MILLIS;
    _sos_next_beep = now + interval;
    playForced("sos_tick:d=32,o=7,b=200:c");
  }
}

// V5: SOS alarm received — siren loops until short press
// Called by UITask::onChannelMessageRecv() when !SOS is detected (V6: listener)
void UITask::triggerSOS(const char* from, const char* text) {
  _sos_active = true;
#ifdef PIN_BUZZER
  // override buzzer_quiet — SOS is an emergency
  buzzer.quiet(false);
  _node_prefs->buzzer_quiet = 0;
  // intentionally not saving prefs — quiet state restored after acknowledgement
  buzzer.play("siren:d=8,o=5,b=100:d,e,d,e,d,e,d,e");
#endif
  _need_refresh = true;
}
