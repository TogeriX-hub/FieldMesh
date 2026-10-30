// FieldMesh repeater: menu on the device display (Station G2).
// Same class as UITask.cpp, only a separate file: UITask.cpp only calls fmRenderPage(),
// fmOnClick() and fmOnLongPress(). Only compiled in with -D FIELDMESH_REPEATER=1.

#include "UITask.h"
#include "target.h"
#include <Arduino.h>

#ifdef FIELDMESH_REPEATER

#ifndef POWEROFF_DELAY
#define POWEROFF_DELAY 3000   // same as in UITask.cpp
#endif

// FieldMesh menu: click = next page, long press = action of the page.
// Power off only on its own page (no accidental off on the status page).
enum { FM_PAGE_STATUS = 0, FM_PAGE_OFFGRID, FM_PAGE_ROOM, FM_PAGE_ROOM_RESET, FM_PAGE_WIFI, FM_PAGE_POWER, FM_PAGE_COUNT };

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

    case FM_PAGE_ROOM: {
      bool room_on = _fm_host && _fm_host->fmIsRoomOn();
      fmDrawHeader("Room server");
      _display->setCursor(0, 15);
      _display->print(room_on ? "State: ON" : "State: OFF");
      if (_fm_host) {
        _display->drawTextEllipsized(0, 26, _display->width(), _fm_host->fmRoomName());
      }
      if (room_on) {
        int clients = 0, posts = 0;
        _fm_host->fmRoomCounts(clients, posts);
        _display->setCursor(0, 37);
        sprintf(tmp, "Clients %d  Posts %d", clients, posts);
        _display->print(tmp);
      }
      _display->setCursor(0, 46);
      _display->print(room_on ? "Hold: switch OFF" : "Hold: switch ON");
      break;
    }

    case FM_PAGE_ROOM_RESET:
      fmDrawHeader("Room reset");
      _display->setCursor(0, 15);
      _display->print("Deletes posts, users,");   // max 21 chars per line
      _display->setCursor(0, 26);
      _display->print("password. Name stays.");
      _display->setCursor(0, 40);
      _display->print(_fm_confirm_until && millis() < _fm_confirm_until ? "Hold again: RESET" : "Hold: reset room");
      break;

    case FM_PAGE_WIFI: {
      bool wifi_on = _fm_host && _fm_host->fmIsWifiOn();
      fmDrawHeader("WiFi / Dashboard");
      _display->setCursor(0, 15);
      _display->print(wifi_on ? "State: ON" : "State: OFF");
      if (wifi_on) {
        _display->drawTextEllipsized(0, 26, _display->width(), _fm_host->fmWifiSsid());
        _display->setCursor(0, 37);
        sprintf(tmp, "192.168.4.1  %d phone%s", _fm_host->fmWifiStations(), _fm_host->fmWifiStations() == 1 ? "" : "s");
        _display->print(tmp);
      }
      _display->setCursor(0, 46);
      _display->print(wifi_on ? "Hold: switch OFF" : "Hold: switch ON");
      break;
    }

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
  _fm_confirm_until = 0;   // leaving the page cancels a pending reset
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
    case FM_PAGE_ROOM:
      if (_fm_host) {
        bool on = !_fm_host->fmIsRoomOn();
        _fm_host->fmSetRoom(on, NULL, NULL);   // last name, or the repeater name
        strcpy(_fm_msg, on ? "Room ON" : "Room OFF");
        _fm_msg_until = millis() + 3000;
      }
      break;
    case FM_PAGE_WIFI:
      if (_fm_host) {
        bool on = !_fm_host->fmIsWifiOn();
        _fm_host->fmSetWifi(on, NULL);
        strcpy(_fm_msg, on ? (_fm_host->fmIsWifiOn() ? "WiFi ON" : "WiFi failed") : "WiFi OFF");
        _fm_msg_until = millis() + 3000;
      }
      break;
    case FM_PAGE_ROOM_RESET:
      if (_fm_host) {
        if (_fm_confirm_until && millis() < _fm_confirm_until) {
          _fm_confirm_until = 0;
          _fm_host->fmRoomReset();
          strcpy(_fm_msg, "Room reset done");
        } else {
          _fm_confirm_until = millis() + 5000;   // second long press within 5 s
          strcpy(_fm_msg, "Hold again to confirm");
        }
        _fm_msg_until = millis() + 5000;
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
