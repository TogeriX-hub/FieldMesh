#include "UITask.h"
#include "target.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

#define POWEROFF_DELAY 3000

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

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _started_at = millis();
  _node_prefs = node_prefs;
  _display->turnOn();

#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)
  user_btn.begin();
#endif

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash){
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
  free(version);
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < _started_at + BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width() / 2, 22, website);

    // version info
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width() / 2, 35, _version_info);

    // node type
#ifdef FIELDMESH_REPEATER
    const char* node_type = "FieldMesh Repeater";   // 18 chars, fits 128 px
#else
    const char* node_type = "< Repeater >";
#endif
    _display->drawTextCentered(_display->width() / 2, 48, node_type);
  } else if (_powering_off_at > 0) {
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width()/ 2, 22, website);

    // Powering off
    const char* poweroff_string = "Turning OFF";
    uint16_t poffWidth = _display->getTextWidth(poweroff_string);
    _display->setCursor((_display->width() - poffWidth) / 2, 48);
    _display->drawTextCentered(_display->width()/2, 48, poweroff_string);
  } else {
#ifdef FIELDMESH_REPEATER
    fmRenderPage();
#else
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(UIColor::primary_txt);
    _display->print(_node_prefs->node_name);

    // freq / sf
    _display->setCursor(0, 20);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
#endif
  }
}

#ifdef FIELDMESH_REPEATER
// FieldMesh menu: click = next page, long press = action of the page.
// Power off only on its own page (no accidental off on the status page).
enum { FM_PAGE_STATUS = 0, FM_PAGE_OFFGRID, FM_PAGE_ROOM, FM_PAGE_WIFI, FM_PAGE_POWER, FM_PAGE_COUNT };

void UITask::fmDrawHeader(const char* title) {
  char tmp[8];
  _display->setTextSize(1);
  _display->setColor(UIColor::primary_txt);
  _display->fillRect(0, 0, _display->width(), 11);
  _display->setColor(UIColor::window_bkg);   // inverted title text
  sprintf(tmp, "%d/%d", _fm_page + 1, FM_PAGE_COUNT);
  int page_w = _display->getTextWidth(tmp);
  _display->drawTextEllipsized(2, 2, _display->width() - page_w - 8, title);
  _display->drawTextRightAlign(_display->width() - 2, 2, tmp);
  _display->setColor(UIColor::primary_txt);
}

void UITask::fmRenderPage() {
  char tmp[40];
  bool offgrid = _fm_host && _fm_host->fmIsOffGrid();

  switch (_fm_page) {
    case FM_PAGE_STATUS:
      fmDrawHeader(_node_prefs->node_name);
      _display->setCursor(0, 15);
      _display->print(offgrid ? "Mode: OFF-GRID" : "Mode: Normal");
      _display->setCursor(0, 26);
      sprintf(tmp, "FREQ: %07.3f SF%d", _node_prefs->freq, _node_prefs->sf);
      _display->print(tmp);
      _display->setCursor(0, 37);
      sprintf(tmp, "BW: %.1f CR: %d", _node_prefs->bw, _node_prefs->cr);
      _display->print(tmp);
      break;

    case FM_PAGE_OFFGRID:
      fmDrawHeader("Off-Grid");
      _display->setCursor(0, 15);
      _display->print(offgrid ? "State: ON" : "State: OFF");
      _display->setCursor(0, 26);
      if (offgrid) {
        sprintf(tmp, "%s %.4f MHz", offGridRegionLabel(_node_prefs->freq), _node_prefs->freq);
      } else {
        sprintf(tmp, "Now: %.3f MHz", _node_prefs->freq);
      }
      _display->print(tmp);
      _display->setCursor(0, 40);
      _display->print(offgrid ? "Hold: switch OFF" : "Hold: switch ON");
      break;

    case FM_PAGE_ROOM:
      fmDrawHeader("Room server");
      _display->setCursor(0, 20);
      _display->print("Not available yet");
      break;

    case FM_PAGE_WIFI:
      fmDrawHeader("WiFi / Dashboard");
      _display->setCursor(0, 20);
      _display->print("Not available yet");
      break;

    case FM_PAGE_POWER:
      fmDrawHeader("Power");
      _display->setCursor(0, 20);
      _display->print("Hold: turn OFF");
      break;
  }

  if (_fm_msg_until && millis() < _fm_msg_until) {   // short feedback after an action
    _display->drawTextCentered(_display->width() / 2, 54, _fm_msg);
  } else {
    _fm_msg_until = 0;
  }
}

void UITask::fmOnClick() {
  if (_display->isOn()) {
    _fm_page = (_fm_page + 1) % FM_PAGE_COUNT;
  } else {
    _display->turnOn();
    _fm_page = FM_PAGE_STATUS;   // always wake up on the status page
  }
  _fm_msg_until = 0;
  _next_refresh = 0;   // redraw immediately
}

void UITask::fmOnLongPress() {
  if (!_display->isOn()) {   // first wake the display, never act blindly
    _display->turnOn();
    _fm_page = FM_PAGE_STATUS;
    _next_refresh = 0;
    return;
  }
  switch (_fm_page) {
    case FM_PAGE_OFFGRID:
      if (_fm_host) {
        bool on = !_fm_host->fmIsOffGrid();
        _fm_host->fmSetOffGrid(on, NULL);
        strcpy(_fm_msg, on ? "Off-Grid ON" : "Off-Grid OFF");
        _fm_msg_until = millis() + 3000;
      }
      break;
    case FM_PAGE_POWER:
      Serial.println("Powering Off");
      _powering_off_at = millis() + POWEROFF_DELAY;
      break;
    default:
      break;   // no action on the other pages
  }
  _next_refresh = 0;   // redraw immediately
}
#endif

void UITask::loop() {
#if defined(PIN_USER_BTN) && defined(DISPLAY_CLASS)
  int ev = user_btn.check();
#ifdef FIELDMESH_REPEATER
  if (ev == BUTTON_EVENT_CLICK) {
    fmOnClick();
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    fmOnLongPress();
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }
#else
  if (ev == BUTTON_EVENT_CLICK) {
    if (_display->isOn()) {
      // TODO: any action ?
    } else {
      _display->turnOn();
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      _display->turnOn();
      Serial.println("Powering Off");
      _powering_off_at = millis() + POWEROFF_DELAY; 
  }
#endif
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;   // refresh every second
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }

  if (_powering_off_at > 0) { // power off timer armed
#if defined(LED_PIN) && defined(LED_STATE_ON)
    digitalWrite(LED_PIN, LED_STATE_ON); // switch on the led until poweroff
    delay(1000);
    digitalWrite(LED_PIN, !LED_STATE_ON); // off LED
#endif
    if (millis() > _powering_off_at) {
      _board->powerOff();  // should not return
    }
  }
}
