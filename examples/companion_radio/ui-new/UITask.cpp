#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

// V5.03: KEY_BACK — physischer Back-Knopf am L1 (back_btn, einfacher Druck).
// Value '\x10' does not collide with any known MeshCore KEY_* value.
#define KEY_BACK  '\x10'

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"

// Formatiert vergangene Zeit als relativen String: "5s", "3m", "2h"
// Identical to the V4 pattern (MsgPreviewScreen, RECENT, TRACKING).
static void formatRelTime(char* buf, size_t buf_size, uint32_t now, uint32_t then) {
  uint32_t secs = (now >= then) ? (now - then) : 0;
  if (secs < 60)        snprintf(buf, buf_size, "%us", secs);
  else if (secs < 3600) snprintf(buf, buf_size, "%um", secs / 60);
  else                  snprintf(buf, buf_size, "%uh", secs / 3600);
}
static float calcDistance(double lat1, double lon1, double lat2, double lon2) {
  if (lat1 == 0.0 && lon1 == 0.0) return -1.0f;  // eigene Position unbekannt
  if (lat2 == 0.0 && lon2 == 0.0) return -1.0f;  // andere Position unbekannt
  const float R = 6371000.0f;  // Erdradius in Metern
  float dLat = (lat2 - lat1) * (M_PI / 180.0f);
  float dLon = (lon2 - lon1) * (M_PI / 180.0f);
  float a = sinf(dLat/2) * sinf(dLat/2) +
            cosf(lat1 * (M_PI / 180.0f)) * cosf(lat2 * (M_PI / 180.0f)) *
            sinf(dLon/2) * sinf(dLon/2);
  float c = 2 * atan2f(sqrtf(a), sqrtf(1-a));
  return R * c;
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    if (display.height() >= 128) {
      // ── Large display (M1/M5, 200px) — two-group layout ─────────────
      // Gruppe oben: Logo + Version + Datum
      int logoWidth = 128;
      display.setColor(DisplayDriver::BLUE);
      display.drawXbm((display.width() - logoWidth) / 2, 4, meshcore_logo, logoWidth, 13);

      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(2);
      display.drawTextCentered(display.width() / 2, 22, _version_info);

      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 42, FIRMWARE_BUILD_DATE);

      // Intentional gap in the middle (42..110)

      // Gruppe unten: "FieldMesh" dominant + "v3" direkt darunter
      display.setTextSize(3);
      display.setColor(DisplayDriver::YELLOW);
      display.drawTextCentered(display.width() / 2, display.height() - 42, "FieldMesh");

      display.setTextSize(2);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, display.height() - 20, "v5");
    } else {
      // ── Kleines Display (Heltec V3/Techo, 64/80px) — kompaktes Layout ─
      int logoWidth = 128;
      display.setColor(DisplayDriver::BLUE);
      display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(2);
      display.drawTextCentered(display.width() / 2, 22, _version_info);

      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 42, FIRMWARE_BUILD_DATE);
    }

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    TRACKING,
    RADIO,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    SOS,     // V3: SOS page (after GPS, before count)
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
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
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(DisplayDriver::GREEN);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // show muted icon if buzzer is muted
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::RED);
      display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;
  
  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0), 
       _shutdown_init(false), sensors_lpp(200) {  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 0);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

    if (_page == HomePage::FIRST) {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);

      // -- Online counter: combine advert senders + message-only senders (no double-counting)
      // Time window: 30 minutes = 1800 seconds
      uint32_t now_ts = _rtc->getCurrentTime();
      const uint32_t ONLINE_WINDOW_SECS = 1800;

      // Use full advert_paths[] ring buffer (ADVERT_PATH_TABLE_SIZE slots), not the
      // 4-entry RECENT display list, so the counter is not capped at UI_RECENT_LIST_SIZE.
      AdvertPath adv_buf[ADVERT_PATH_TABLE_SIZE];
      int adv_count = the_mesh.getRecentlyHeard(adv_buf, ADVERT_PATH_TABLE_SIZE);
      int adv_fresh = 0;
      for (int i = 0; i < adv_count; i++) {
        if (adv_buf[i].name[0] == 0) continue;
        uint32_t age = (now_ts >= adv_buf[i].recv_timestamp)
                       ? (now_ts - adv_buf[i].recv_timestamp) : 0;
        if (age <= ONLINE_WINDOW_SECS) adv_fresh++;
      }
      // Only add message-cache entries whose name does NOT already appear in the
      // fresh advert list — prevents double-counting nodes that both advertised
      // and sent a direct/group message within the window.
      int online_total = _task->countOnlineNodesExcluding(now_ts, ONLINE_WINDOW_SECS,
                                                           adv_buf, adv_count) + adv_fresh;

      // -- Mode flags
      bool gps_share_on = the_mesh.isAutoAdvertEnabled();
      bool off_grid_on  = the_mesh.isOffGridActive();
      bool connected    = _task->hasConnection();

      // -- Favourite info
      const char* fav_name = _task->getLatestFavoriteName();
      bool has_fav = (fav_name != nullptr && fav_name[0] != 0);
      char fav_line[48] = "";
      if (has_fav) {
        uint32_t fav_time = _task->getLatestFavoriteTime();
        char rel_time[8];
        formatRelTime(rel_time, sizeof(rel_time), now_ts, fav_time);
        char filtered_fav[33];
        display.translateUTF8ToBlocks(filtered_fav, fav_name, sizeof(filtered_fav));
        snprintf(fav_line, sizeof(fav_line), "* %s  %s", filtered_fav, rel_time);
      }

      if (display.height() >= 128) {
        // == LARGE DISPLAY (M1, 200px) =========================================

        // Line 1 (y=22): "< Connected >" (size 1) when connected, MSG:X (size 2) when disconnected
        if (connected) {
          display.drawTextCentered(display.width() / 2, 22, "< Connected >");
        } else {
          display.setTextSize(2);
          sprintf(tmp, "MSG: %d", _task->getNumUnread());
          display.drawTextCentered(display.width() / 2, 22, tmp);
          display.setTextSize(1);
        }

        // Line 2 (y=33): online counter — always in the same position
        int y = 33;
        char online_str[16];
        snprintf(online_str, sizeof(online_str), " %d nodes", online_total);
        display.drawXbm(0, y + 2, nodes_icon, 8, 8);   // +2px: center icon with GFX proportional font text
        display.setCursor(10, y);
        display.print(online_str);
        y += 11;

        // Mode status line — both modes on one line, only shown when at least one is active
        if (gps_share_on || off_grid_on) {
          char mode_str[32] = "";
          if (gps_share_on) strncat(mode_str, "[GPS-SHARE] ", sizeof(mode_str) - strlen(mode_str) - 1);
          if (off_grid_on)  strncat(mode_str, "[OFF-GRID]",  sizeof(mode_str) - strlen(mode_str) - 1);
          display.setCursor(0, y);
          display.print(mode_str);
          y += 11;
        }

        // Favourite line (disconnected only)
        if (!connected && has_fav) {
          display.setCursor(0, y);
          display.print(fav_line);
        }

      } else {
        // == SMALL DISPLAY (L1, 64px) ==========================================

        // Line 1 (y=22): MSG:X left + [X] right — both TextSize 1
        if (!connected) {
          sprintf(tmp, "MSG: %d", _task->getNumUnread());
          display.setCursor(0, 22);
          display.print(tmp);
        }

        // online node count: filled circle icon + number, right-aligned
        char online_str[8];
        snprintf(online_str, sizeof(online_str), " %d", online_total);
        int online_w = display.getTextWidth(online_str) + 8 + 2; // icon(8) + gap(2) + text
        int icon_x = display.width() - online_w - 1;
        display.drawXbm(icon_x, 22, nodes_icon, 8, 8);
        display.setCursor(icon_x + 8 + 2, 22);
        display.print(online_str);

        // Line 2 (y=33): priority order: connected > favourite > mode > empty
        if (connected) {
          display.drawTextCentered(display.width() / 2, 33, "< Connected >");
        } else if (has_fav) {
          display.drawTextEllipsized(0, 33, display.width(), fav_line);
        } else if (gps_share_on && off_grid_on) {
          display.setCursor(0, 33);
          display.print("[GPS-SHARE][OG]");
        } else if (gps_share_on) {
          display.setCursor(0, 33);
          display.print("[GPS-SHARE]");
        } else if (off_grid_on) {
          display.setCursor(0, 33);
          display.print("[OFF-GRID]");
        }
        // else: line 2 stays empty
      }

      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 54, tmp);
      #endif

      // Footer hint line
      display.setCursor(0, display.height() - 13);
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
#if UI_HAS_JOYSTICK
      display.print("Messages: press Enter");
#else
      display.print("Messages: longpress");
#endif
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::GREEN);
      int y = 20;
      int max_y = (display.height() >= 128) ? 53 : 42;
      for (int i = 0; i < UI_RECENT_LIST_SIZE && y <= max_y; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }
        
        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;
        
        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, display.height() - 13, "Advert: " PRESS_LABEL);
    } else if (_page == HomePage::TRACKING) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      bool own_gps = (sensors.node_lat != 0.0 || sensors.node_lon != 0.0);
#if ENV_INCLUDE_GPS == 1
      LocationProvider* loc = sensors.getLocationProvider();
      own_gps = own_gps && loc != NULL && loc->isValid();
#endif
      int y = 20;
      int max_y = (display.height() >= 128) ? 53 : 42;
      // display-height-adaptive limit: 3 nodes on small display (y<=42), 4 on large (y<=53)
      // ensures footer line at display.height()-11 is never overlapped
      for (int i = 0; i < UI_RECENT_LIST_SIZE && y <= max_y; i++) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;

        // Nur Favoriten anzeigen (flags LSB = 1 -> Favorit)
        ContactInfo* ci = the_mesh.lookupContactByPubKey(a->pubkey_prefix, sizeof(a->pubkey_prefix));
        if (ci == NULL || !(ci->flags & 0x01)) continue;

        // Only show if at least one advert with GPS was received
        if (!a->has_gps) continue;

        char time_buf[8];
        int secs = _rtc->getCurrentTime() - a->gps_timestamp;  // Zeit seit letztem GPS-Advert
        if (secs < 60)        sprintf(time_buf, "%ds", secs);
        else if (secs < 3600) sprintf(time_buf, "%dm", secs / 60);
        else                  sprintf(time_buf, "%dh", secs / 3600);

        char dist_buf[10];
        if (!own_gps) {
          strcpy(dist_buf, "---");
        } else if (ci == NULL || (ci->gps_lat == 0 && ci->gps_lon == 0)) {
          strcpy(dist_buf, "?");
        } else {
          float dist = calcDistance(
            sensors.node_lat, sensors.node_lon,
            ci->gps_lat / 1000000.0, ci->gps_lon / 1000000.0
          );
          if (dist < 0)         strcpy(dist_buf, "?");
          else if (dist < 1000) sprintf(dist_buf, "%dm", (int)dist);
          else                  sprintf(dist_buf, "%.1fkm", dist / 1000.0f);
        }

        display.setColor(secs < 300 ? DisplayDriver::GREEN : DisplayDriver::YELLOW);

        char right_buf[20];
        sprintf(right_buf, "%s %s", time_buf, dist_buf);
        int right_w = display.getTextWidth(right_buf);
        int max_name_w = display.width() - right_w - 2;
        char filtered_tname[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_tname, a->name, sizeof(filtered_tname));
        display.drawTextEllipsized(0, y, max_name_w, filtered_tname);
        display.setCursor(display.width() - right_w - 1, y);
        display.print(right_buf);
        y += 11;  // only increment if the node was actually rendered
      }

      display.setColor(DisplayDriver::GREEN);
#if UI_HAS_JOYSTICK
      display.drawTextCentered(display.width() / 2, display.height() - 13, "Settings: press Enter");
#else
      display.drawTextCentered(display.width() / 2, display.height() - 13, "Settings: long press");
#endif

    } else if (_page == HomePage::RADIO) {
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
      } else {
        strcpy(buf, gps_state ? "gps on" : "gps off");
      }
#else
      strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y = y + 12;
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        bool gps_actually_valid = nmea->isEnabled() && nmea->isValid();
        strcpy(buf, gps_actually_valid ? "fix" : "no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "sat");
        if (gps_actually_valid) {
          sprintf(buf, "%d", nmea->satellitesCount());
        } else {
          strcpy(buf, "---");
        }
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "pos");
        if (gps_actually_valid) {
          sprintf(buf, "%.4f %.4f",
            nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        } else {
          strcpy(buf, "---");
        }
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "alt");
        if (gps_actually_valid) {
          sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        } else {
          strcpy(buf, "---");
        }
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.print(name);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SOS) {
      // V3: SOS page — icon + hint for long press
      if (display.height() >= 128) {
        // Large display: "SOS" + icon in upper half
        // Icon at same position as SOSSendScreen (display.height()/2 - 24)
        display.setColor(DisplayDriver::RED);
        display.setTextSize(3);
        display.drawTextCentered(display.width() / 2, 30, "SOS");
        display.setTextSize(1);
        display.setColor(DisplayDriver::LIGHT);
        display.drawXbm((display.width() - 48) / 2, display.height() / 2 - 24, advert_icon_large, 48, 48);
      } else {
        // Small display: compact icon, keep existing size
        display.setColor(DisplayDriver::LIGHT);
        display.drawXbm((display.width() - 32) / 2, 16, advert_icon, 32, 32);
      }
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, display.height() - 13, "SOS: " PRESS_LABEL);
    }
    return 5000;   // next render after 5000 ms
  }

  bool handleInput(char c) override {
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      if (_page == HomePage::TRACKING) {
        _task->showAlert("Tracking", 800);
      }
      return true;
    }
#if UI_HAS_JOYSTICK
    // V5: L1 — short joystick press on FIRST page opens MsgFilterScreen
    if (c == KEY_ENTER && _page == HomePage::FIRST) {
      _task->gotoMsgFilter();
      return true;
    }
#endif
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    return false;
  }

  bool isOnRecentPage()   const { return _page == HomePage::RECENT; }
  bool isOnTrackingPage() const { return _page == HomePage::TRACKING; }
  bool isOnSOSPage()      const { return _page == HomePage::SOS; }       // V3
  bool isOnFirstPage()    const { return _page == HomePage::FIRST; }     // V5: E6
};

// ── Message History Screen ─────────────────────────────────────────────────
// Replaces MsgPreviewScreen. Shows one message per view, scrollable.
// Owns the MsgEntry ring buffer for all devices.
class MsgHistoryScreen : public UIScreen {
  UITask*          _task;
  mesh::RTCClock*  _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char from_name[33];
    char channel_tag[16];
    char msg[80];
    bool is_favorite;
    bool valid;           // true = Eintrag belegt, false = geloescht/leer
  };
  #define MAX_HISTORY_MSGS 32
  int num_total;            // number of valid entries in RAM = message counter on HomeScreen
  int head;                 // zuletzt hinzugefuegter Index
  int display_idx;          // aktuell angezeigte Nachricht
  bool _show_favorites_only;
  MsgEntry history[MAX_HISTORY_MSGS];

  // Search backwards from start_idx for the first valid (and visible) entry
  int firstValidFrom(int start_idx) const {
    for (int i = 0; i < MAX_HISTORY_MSGS; i++) {
      int idx = (start_idx + MAX_HISTORY_MSGS - i) % MAX_HISTORY_MSGS;
      if (history[idx].valid) {
        if (!_show_favorites_only || history[idx].is_favorite) return idx;
      }
    }
    return -1; // no valid entry found
  }

  // Find next older valid and visible entry from current_idx
  int nextValidFrom(int current_idx) const {
    for (int i = 1; i < MAX_HISTORY_MSGS; i++) {
      int idx = (current_idx + MAX_HISTORY_MSGS - i) % MAX_HISTORY_MSGS;
      if (history[idx].valid) {
        if (!_show_favorites_only || history[idx].is_favorite) return idx;
      }
    }
    return -1;
  }

  bool hasAnyFavorite() const {
    for (int i = 0; i < MAX_HISTORY_MSGS; i++) {
      if (history[i].valid && history[i].is_favorite) return true;
    }
    return false;
  }

  // Delete entry from RAM and update HomeScreen counter
  void deleteEntry(int idx) {
    if (!history[idx].valid) return;
    bool was_favorite = history[idx].is_favorite;
    memset(&history[idx], 0, sizeof(MsgEntry)); // valid=false durch memset
    if (num_total > 0) num_total--;
    _task->setUnreadCount(num_total);
    if (was_favorite && !hasAnyFavorite()) {
      _task->clearFavoriteCache();
    }
  }

public:
  MsgHistoryScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), num_total(0),
      head(MAX_HISTORY_MSGS - 1), display_idx(0), _show_favorites_only(false) {
    memset(history, 0, sizeof(history));
  }

  // Called by MsgFilterScreen before setCurrScreen()
  void setFilter(bool favorites_only) {
    _show_favorites_only = favorites_only;
    int v = firstValidFrom(head);
    display_idx = (v >= 0) ? v : head;
  }

  // Add new message — only when app is NOT connected (called by UITask::newMsg)
  void addMessage(uint8_t path_len, const char* from_name, const char* msg_text, bool is_favorite = false, const char* channel_name = nullptr) {
    head = (head + 1) % MAX_HISTORY_MSGS;

    // If the new head slot was still occupied (ring buffer full): correct the counter
    if (history[head].valid) {
      if (num_total > 0) num_total--;
    }

    auto p = &history[head];
    memset(p, 0, sizeof(MsgEntry));
    p->valid       = true;
    p->timestamp   = _rtc->getCurrentTime();
    p->is_favorite = is_favorite;

    strncpy(p->from_name, from_name ? from_name : "", sizeof(p->from_name) - 1);

    if (path_len == 0xFF) {
      strcpy(p->channel_tag, "[DM]");
    } else if (channel_name && channel_name[0]) {
      strncpy(p->channel_tag, channel_name, sizeof(p->channel_tag) - 1);
      p->channel_tag[sizeof(p->channel_tag) - 1] = 0;
    } else {
      strcpy(p->channel_tag, "[CH]");
    }

    strncpy(p->msg, msg_text ? msg_text : "", sizeof(p->msg) - 1);

    num_total++;
    display_idx = head;
    _task->setUnreadCount(num_total); // HomeScreen-Zaehler aktualisieren
  }

  // Delete all entries — on app connect sync and on long press
  void clearAll() {
    memset(history, 0, sizeof(history));
    num_total  = 0;
    head       = MAX_HISTORY_MSGS - 1;
    display_idx = 0;
    _task->setUnreadCount(0);
    _task->clearFavoriteCache();
  }

  int getNumTotal() const { return num_total; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    // Ensure display_idx is valid and visible
    if (!history[display_idx].valid ||
        (_show_favorites_only && !history[display_idx].is_favorite)) {
      int v = firstValidFrom(head);
      display_idx = (v >= 0) ? v : head;
    }

    // Zaehle sichtbare Eintraege
    int visible = 0;
    for (int i = 0; i < MAX_HISTORY_MSGS; i++) {
      if (history[i].valid && (!_show_favorites_only || history[i].is_favorite)) visible++;
    }

    if (visible == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.fillRect(0, 0, display.width(), 11);
      display.setColor(DisplayDriver::DARK);
      display.drawTextCentered(display.width() / 2, 1, "Messages");
      display.setColor(DisplayDriver::LIGHT);
      const char* msg = _show_favorites_only ? "No favorites" : "No messages";
      display.drawTextCentered(display.width() / 2, display.height() / 2 - 4, msg);
#if !UI_HAS_JOYSTICK
      display.setCursor(0, display.height() - 13);
      display.print("dbl=back  long=clear");
#endif
      return 5000;
    }

    auto p = &history[display_idx];

    // Title bar: channel tag + name left-aligned, time right-aligned (M1)
    // Auf L1: zentriert (wenig Platz)
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);

    char filtered_name[33];
    display.translateUTF8ToBlocks(filtered_name, p->from_name, sizeof(filtered_name));

    // Prepare timestamp (needed for both layouts)
    char time_buf[8];
    uint32_t ts = p->timestamp;
    formatRelTime(time_buf, sizeof(time_buf), _rtc->getCurrentTime(), ts);

    if (display.height() >= 128) {
      // M1: channel tag + name left-aligned, * immediately after, time right-aligned
      // Set time on the right first so the available left space is known
      int tw = display.getTextWidth(time_buf);
      display.setCursor(display.width() - tw - 2, 1);
      display.print(time_buf);

      // Name left-aligned: "Public *" (channel) or "[DM] NodeName *" (direct)
      char left_part[48];
      bool is_dm = (strcmp(p->channel_tag, "[DM]") == 0);
      if (is_dm) {
        snprintf(left_part, sizeof(left_part), p->is_favorite ? "%s %s *" : "%s %s", p->channel_tag, filtered_name);
      } else {
        snprintf(left_part, sizeof(left_part), p->is_favorite ? "%s *" : "%s", p->channel_tag);
      }
      display.setCursor(0, 1);
      display.print(left_part);
    } else {
      // L1: Platz knapp — zentriert wie bisher
      bool is_dm = (strcmp(p->channel_tag, "[DM]") == 0);
      char title[48];
      if (is_dm) {
        snprintf(title, sizeof(title), "%s %s", p->channel_tag, filtered_name);
      } else {
        snprintf(title, sizeof(title), "%s", p->channel_tag);
      }
      if (p->is_favorite) {
        if (strlen(title) < sizeof(title) - 3) strcat(title, " *");
      }
      display.drawTextCentered(display.width() / 2, 1, title);
    }
    display.setColor(DisplayDriver::LIGHT);

    if (display.height() >= 128) {
      // M1: Trennlinie + Nachrichtentext + Fusszeile
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(0, 12, display.width(), 1);

      char filtered_msg[sizeof(p->msg)];
      display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
      display.setCursor(0, 15);
      display.printWordWrap(filtered_msg, display.width());

      display.setCursor(0, display.height() - 13);
      display.print("short=next  dbl=back");
    } else {
      // L1: time in yellow, then text
      display.setCursor(0, 13);
      display.setColor(DisplayDriver::YELLOW);
      display.print(time_buf);

      char filtered_msg[sizeof(p->msg)];
      display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
      display.setCursor(0, 24);
      display.setColor(DisplayDriver::LIGHT);
      display.printWordWrap(filtered_msg, display.width());
      // No footer on L1 — back key is a physical button
    }

#if AUTO_OFF_MILLIS == 0
    return 10000;
#else
    return 1000;
#endif
  }

  bool handleInput(char c) override {
#if UI_HAS_JOYSTICK
    if (c == KEY_NEXT) {
#else
    if (c == KEY_NEXT || c == KEY_RIGHT) {
#endif
      // Delete current message, navigate to next
      int next = nextValidFrom(display_idx);
      deleteEntry(display_idx);
      if (next >= 0 && history[next].valid) {
        display_idx = next;
      } else {
        // No further message — return to HomeScreen
        _task->gotoHomeScreen();
      }
      return true;
    }

    // Delete all: long press only (KEY_SELECT via handleLongPress) — M1 and L1 identical.
    // KEY_ENTER (short joystick press on L1) is intentionally ignored.
    if (c == KEY_SELECT) {
      clearAll();
      _task->gotoHomeScreen();
      return true;
    }

#if UI_HAS_JOYSTICK
    // L1: back key → return to MsgFilterScreen (without deleting)
    if (c == KEY_BACK) {
      _task->gotoMsgFilter();
      return true;
    }
#else
    // M1: double-click (KEY_PREV) = return to MsgFilterScreen (without deleting)
    if (c == KEY_PREV) {
      _task->gotoMsgFilter();
      return true;
    }
#endif
    return false;
  }
};


// ── Message Filter Screen ──────────────────────────────────────────────────
// Menu: filter selection (All / Favorites) + on L1 also send entry and back.
class MsgFilterScreen : public UIScreen {
  UITask* _task;
  UIScreen* _history;

  enum Filter { ALL = 0, FAVORITES = 1 } _filter;

#if UI_HAS_JOYSTICK
  enum Row { ROW_SHOW = 0, ROW_SEND, ROW_BACK, ROW_COUNT } _cursor;
#else
  enum Row { ROW_ALL = 0, ROW_FAVORITES, ROW_BACK, ROW_COUNT } _cursor;
#endif

public:
  MsgFilterScreen(UITask* task, UIScreen* history)
    : _task(task), _history(history), _filter(ALL)
#if UI_HAS_JOYSTICK
    , _cursor(ROW_SHOW)
#else
    , _cursor(ROW_ALL)
#endif
  {}

  Filter getFilter() const { return _filter; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    // Titelzeile invertiert
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    display.drawTextCentered(display.width() / 2, 1, "Messages");
    display.setColor(DisplayDriver::LIGHT);

#if UI_HAS_JOYSTICK
    // L1: 3-line menu
    const char* rows[] = { NULL, "Send Message", "Back" };
    for (uint8_t i = 0; i < ROW_COUNT; i++) {
      int y = 15 + i * 14;
      bool sel = (i == (uint8_t)_cursor);
      if (sel) {
        display.fillRect(0, y - 1, display.width(), 12);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.setCursor(0, y);
      display.print(sel ? ">" : " ");
      display.setCursor(8, y);
      if (i == ROW_SHOW) {
        // "Show: [All      ]" oder "Show: [Favorites]"
        display.print("Show: [");
        display.print(_filter == ALL ? "All      " : "Favorites");
        display.print("]");
      } else {
        display.print(rows[i]);
      }
      display.setColor(DisplayDriver::LIGHT);
    }
#else
    // M1: 3-line menu (All / Favorites / Back) — analogous to OutdoorMenuScreen
    const char* labels[] = { "[ All ]", "[ Favorites ]", "Back" };
    for (uint8_t i = 0; i < ROW_COUNT; i++) {
      int y = 16 + i * 16;
      bool sel = (i == (uint8_t)_cursor);
      display.setCursor(0, y);
      display.setColor(sel ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);
      display.print(sel ? "> " : "  ");
      display.print(labels[i]);
    }
    // M1: Hinweistext
    display.setCursor(0, display.height() - 13);
    display.setColor(DisplayDriver::LIGHT);
    display.print("short=next  long=select");
#endif
    return 200;
  }

  bool handleInput(char c) override {
#if UI_HAS_JOYSTICK
    if (c == KEY_PREV) {
      _cursor = (Row)(((int)_cursor + ROW_COUNT - 1) % ROW_COUNT);
      return true;
    }
    if (c == KEY_NEXT) {
      _cursor = (Row)(((int)_cursor + 1) % ROW_COUNT);
      return true;
    }
    // L/R on ROW_SHOW: toggle filter
    if ((c == KEY_LEFT || c == KEY_RIGHT) && _cursor == ROW_SHOW) {
      _filter = (_filter == ALL) ? FAVORITES : ALL;
      return true;
    }
    if (c == KEY_ENTER) {
      switch (_cursor) {
        case ROW_SHOW:
          ((MsgHistoryScreen*)_history)->setFilter(_filter == FAVORITES);
          _task->setCurrScreen(_history);
          break;
        case ROW_SEND:
          _task->gotoChannelSelect();
          break;
        case ROW_BACK:
          _task->gotoHomeScreen();
          break;
        default: break;
      }
      return true;
    }
    // Back button or KEY_SELECT → return to HomeScreen
    if (c == KEY_SELECT || c == KEY_BACK) {
      _task->gotoHomeScreen();
      return true;
    }
#else
    // M1: kurzer Druck = naechster Eintrag, langer Druck = Auswahl (via handleLongPress → KEY_ENTER)
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _cursor = (Row)(((int)_cursor + 1) % ROW_COUNT);
      return true;
    }
    if (c == KEY_PREV || c == KEY_LEFT) {
      _cursor = (Row)(((int)_cursor + ROW_COUNT - 1) % ROW_COUNT);
      return true;
    }
    if (c == KEY_ENTER) {
      // Execute selection
      if (_cursor == ROW_BACK) {
        _task->gotoHomeScreen();
      } else {
        ((MsgHistoryScreen*)_history)->setFilter(_cursor == ROW_FAVORITES);
        _task->setCurrScreen(_history);
      }
      return true;
    }
#endif
    return false;
  }
};

// ── Favoriten-Popup Screen ─────────────────────────────────────────────────
// Opens automatically when a message from a favourite arrives.
// Always shows only the LAST favourite message (one slot, no buffer).
// Wegdruecken loescht nichts — Nachricht bleibt in MsgHistoryScreen erhalten.
// M1: kurzer Druck = weg (longpress tut nichts)
// L1: back-Knopf = weg, kurzer Joystick-Druck = weg (longpress tut nichts)
class FavPreviewScreen : public UIScreen {
  UITask*         _task;
  mesh::RTCClock* _rtc;

  struct {
    uint32_t timestamp;
    char     from_name[33];
    char     channel_tag[16];
    char     msg[80];
  } _entry;
  bool _has_entry = false;

public:
  FavPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) {}

  // Called by UITask::newMsg() — always overwrites the last entry.
  void setMessage(uint8_t path_len, const char* from_name, const char* text, const char* channel_name = nullptr) {
    _entry.timestamp = _rtc->getCurrentTime();
    strncpy(_entry.from_name, from_name, sizeof(_entry.from_name) - 1);
    _entry.from_name[sizeof(_entry.from_name) - 1] = 0;
    if (path_len == 0xFF) {
      strncpy(_entry.channel_tag, "[DM]", sizeof(_entry.channel_tag));
    } else if (channel_name && channel_name[0]) {
      strncpy(_entry.channel_tag, channel_name, sizeof(_entry.channel_tag) - 1);
      _entry.channel_tag[sizeof(_entry.channel_tag) - 1] = 0;
    } else {
      strncpy(_entry.channel_tag, "[CH]", sizeof(_entry.channel_tag));
    }
    strncpy(_entry.msg, text, sizeof(_entry.msg) - 1);
    _entry.msg[sizeof(_entry.msg) - 1] = 0;
    _has_entry = true;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    if (!_has_entry) {
      // Sollte nie passieren — sicherheitshalber
      _task->gotoHomeScreen();
      return 100;
    }

    // Titelzeile: Channel-Tag + Name + * (invertiert wie ueberall)
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    char filtered_name[33];
    display.translateUTF8ToBlocks(filtered_name, _entry.from_name, sizeof(filtered_name));

    // Zeitstempel
    char time_buf[8];
    uint32_t ts = _entry.timestamp;
    formatRelTime(time_buf, sizeof(time_buf), _rtc->getCurrentTime(), ts);

    if (display.height() >= 128) {
      // M1: time right-aligned, name + channel tag + * left-aligned (analogous to MsgHistoryScreen)
      int tw = display.getTextWidth(time_buf);
      display.setCursor(display.width() - tw - 2, 1);
      display.print(time_buf);

      char left_part[48];
      if (strcmp(_entry.channel_tag, "[DM]") == 0) {
        snprintf(left_part, sizeof(left_part), "%s %s *", _entry.channel_tag, filtered_name);
      } else {
        snprintf(left_part, sizeof(left_part), "%s *", _entry.channel_tag);
      }
      display.setCursor(0, 1);
      display.print(left_part);
    } else {
      // L1: zentriert
      char title[48];
      if (strcmp(_entry.channel_tag, "[DM]") == 0) {
        snprintf(title, sizeof(title), "%s %s *", _entry.channel_tag, filtered_name);
      } else {
        snprintf(title, sizeof(title), "%s *", _entry.channel_tag);
      }
      display.drawTextCentered(display.width() / 2, 1, title);
    }
    display.setColor(DisplayDriver::LIGHT);

    if (display.height() >= 128) {
      // M1: Trennlinie + Nachrichtentext
      display.drawRect(0, 12, display.width(), 1);
      char filtered_msg[sizeof(_entry.msg)];
      display.translateUTF8ToBlocks(filtered_msg, _entry.msg, sizeof(filtered_msg));
      display.setCursor(0, 15);
      display.printWordWrap(filtered_msg, display.width());
      // No footer — this is a popup, operation is self-explanatory
    } else {
      // L1: time in yellow, then text
      display.setCursor(0, 13);
      display.setColor(DisplayDriver::YELLOW);
      display.print(time_buf);

      char filtered_msg[sizeof(_entry.msg)];
      display.translateUTF8ToBlocks(filtered_msg, _entry.msg, sizeof(filtered_msg));
      display.setCursor(0, 24);
      display.setColor(DisplayDriver::LIGHT);
      display.printWordWrap(filtered_msg, display.width());
    }

#if AUTO_OFF_MILLIS == 0
    return 10000;
#else
    return 1000;
#endif
  }

  bool handleInput(char c) override {
    // M1: kurzer Druck (KEY_NEXT) = weg
    // L1: back-Knopf (KEY_BACK) oder kurzer Joystick-Druck (KEY_ENTER) = weg
    // longpress tut in diesem Screen bewusst nichts
    if (c == KEY_NEXT || c == KEY_BACK || c == KEY_ENTER) {
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

// ── Message Compose Screen (L1 only) ───────────────────────────────────────
// Tastatur-Screen zum Tippen einer Nachricht. 4-Zeilen-QWERTY-Grid.
// Muss VOR ChannelSelectScreen definiert sein (Cast in ChannelSelectScreen::handleInput).
#if UI_HAS_JOYSTICK
class MsgComposeScreen : public UIScreen {
  UITask*   _task;
  UIScreen* _channel_select;  // accessible for ChannelSelectScreen::setCompose() (set in constructor)
  uint8_t   _channel_idx;

  char    _text[21];   // max 20 Zeichen + Nullterminator
  uint8_t _text_len;
  uint8_t _row;
  uint8_t _col;

  // Keyboard-Layout — 4 Zeilen
  // '_' = SPACE, '<' = BACKSPACE, '~' = SEND-Taste
  static const char* const ROWS[4];
  static const uint8_t ROW_LENS[4];

  void _doSend() {
    if (_text_len == 0) {
      _task->showAlert("Empty!", 1000);
      return;
    }
    if (the_mesh.sendChannelMessage(_channel_idx, _text)) {
      _task->notify(UIEventType::ack);
      _task->showAlert("Sent!", 1500);
    } else {
      _task->showAlert("Send failed!", 2000);
    }
    _task->gotoMsgFilter();
  }

public:
  MsgComposeScreen(UITask* task, UIScreen* channel_select)
    : _task(task), _channel_select(channel_select),
      _channel_idx(0), _text_len(0), _row(1), _col(0) {
    _text[0] = 0;
  }

  void reset(uint8_t channel_idx) {
    _channel_idx = channel_idx;
    _text[0] = 0;
    _text_len = 0;
    _row = 1;  // start on QWERTY row
    _col = 0;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    // Titelzeile: Channel-Name
    ChannelDetails ch;
    const char* ch_name = "?";
    char ch_name_buf[17];
    if (the_mesh.getChannel(_channel_idx, ch)) {
      strncpy(ch_name_buf, ch.name, 16);
      ch_name_buf[16] = 0;
      ch_name = ch_name_buf;
    }
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    char title[32];
    snprintf(title, sizeof(title), "Send > %s", ch_name);
    display.drawTextCentered(display.width() / 2, 1, title);
    display.setColor(DisplayDriver::LIGHT);

    // Input line with cursor
    display.setCursor(0, 13);
    display.print(_text);
    int cx = display.getTextWidth(_text);
    if (cx < display.width() - 4) {
      display.fillRect(cx, 12, 5, 9);  // Cursor-Block invertiert
    }

    // Character counter top-right (does not overlap title bar)
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%d/20", _text_len);
    int cw = display.getTextWidth(cnt);
    display.setCursor(display.width() - cw - 1, 1);
    display.setColor(DisplayDriver::DARK);  // im invertierten Titelbereich
    display.print(cnt);
    display.setColor(DisplayDriver::LIGHT);

    // Keyboard-Grid — 4 Zeilen ab y=24, je 10px Abstand
    for (uint8_t r = 0; r < 4; r++) {
      int y = 24 + r * 10;
      if (y > display.height()) break;
      const char* row = ROWS[r];
      uint8_t rlen = ROW_LENS[r];
      int cell_w = display.width() / rlen;

      for (uint8_t col = 0; col < rlen; col++) {
        int x = col * cell_w;
        bool sel = (r == _row && col == _col);
        char ch_char = row[col];

        if (sel) {
          display.fillRect(x, y - 1, cell_w - 1, 9);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(x + 1, y);
        // Sonderzeichen lesbarer darstellen
        if (ch_char == '_')      display.print("_");   // SPACE
        else if (ch_char == '<') display.print("<");   // BACKSPACE
        else if (ch_char == '~') display.print("OK");  // SEND
        else {
          char s[2] = { ch_char, 0 };
          display.print(s);
        }
        display.setColor(DisplayDriver::LIGHT);
      }
    }

    return 100;  // fast refresh for smooth input
  }

  bool handleInput(char c) override {
    if (c == KEY_PREV) {
      _row = (_row + 3) % 4;
      if (_col >= ROW_LENS[_row]) _col = ROW_LENS[_row] - 1;
      return true;
    }
    if (c == KEY_NEXT) {
      _row = (_row + 1) % 4;
      if (_col >= ROW_LENS[_row]) _col = ROW_LENS[_row] - 1;
      return true;
    }
    if (c == KEY_LEFT) {
      _col = (_col + ROW_LENS[_row] - 1) % ROW_LENS[_row];
      return true;
    }
    if (c == KEY_RIGHT) {
      _col = (_col + 1) % ROW_LENS[_row];
      return true;
    }
    if (c == KEY_ENTER) {
      // Zeichen einfuegen
      char ch_char = ROWS[_row][_col];
      if (ch_char == '_') {
        if (_text_len < 20) { _text[_text_len++] = ' '; _text[_text_len] = 0; }
      } else if (ch_char == '<') {
        if (_text_len > 0) { _text[--_text_len] = 0; }
      } else if (ch_char == '~') {
        _doSend();
      } else {
        if (_text_len < 20) { _text[_text_len++] = ch_char; _text[_text_len] = 0; }
      }
      return true;
    }
    if (c == KEY_SELECT) {
      // Langer Joystick-Druck = Senden
      _doSend();
      return true;
    }
    // Back key = cancel, return to ChannelSelectScreen
    if (c == KEY_BACK) {
      _task->gotoChannelSelect();
      return true;
    }
    return false;
  }
};

// Keyboard-Daten als static member
const char* const MsgComposeScreen::ROWS[4] = {
  "1234567890.",    // Zeile 0: Ziffern
  "QWERTYUIOP",    // Zeile 1: QWERTY
  "ASDFGHJKL,",    // Zeile 2: ASDF
  "ZXCVBNM_<~"     // Zeile 3: ZXCV + SPACE + BACKSPACE + SEND
};
const uint8_t MsgComposeScreen::ROW_LENS[4] = { 11, 10, 10, 10 };

// ── Channel Select Screen (L1 only) ────────────────────────────────────────
// Shows available channels; user selects one before composing a message.
// MsgComposeScreen must already be defined (cast in handleInput).
class ChannelSelectScreen : public UIScreen {
  UITask*   _task;
  UIScreen* _compose;

  static const uint8_t MAX_CH = 10;
  uint8_t  _ch_indices[MAX_CH];
  char     _ch_names[MAX_CH][17];
  uint8_t  _ch_count;
  uint8_t  _cursor;

public:
  ChannelSelectScreen(UITask* task, UIScreen* compose)
    : _task(task), _compose(compose), _ch_count(0), _cursor(0) {}

  void setCompose(UIScreen* compose) { _compose = compose; }

  void loadChannels() {
    _ch_count = 0;
    _cursor = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS && _ch_count < MAX_CH; i++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch)) continue;
      if (ch.name[0] == 0) continue;
      _ch_indices[_ch_count] = (uint8_t)i;
      strncpy(_ch_names[_ch_count], ch.name, 16);
      _ch_names[_ch_count][16] = 0;
      _ch_count++;
    }
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    // Titelzeile
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    display.drawTextCentered(display.width() / 2, 1, "Select Channel");
    display.setColor(DisplayDriver::LIGHT);

    if (_ch_count == 0) {
      display.drawTextCentered(display.width() / 2, display.height() / 2 - 4, "No channels");
      return 2000;
    }

    for (uint8_t i = 0; i < _ch_count; i++) {
      int y = 15 + i * 12;
      if (y > display.height() - 12) break;
      bool sel = (i == _cursor);
      if (sel) {
        display.fillRect(0, y - 1, display.width(), 11);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.setCursor(0, y);
      display.print(sel ? ">" : " ");
      display.setCursor(8, y);
      display.print(_ch_names[i]);
      display.setColor(DisplayDriver::LIGHT);
    }
    return 200;
  }

  bool handleInput(char c) override {
    if (c == KEY_PREV) {
      if (_cursor > 0) _cursor--;
      return true;
    }
    if (c == KEY_NEXT) {
      if (_cursor < _ch_count - 1) _cursor++;
      return true;
    }
    if (c == KEY_ENTER && _ch_count > 0) {
      // Initialise MsgComposeScreen with the selected channel and open it
      if (_compose != NULL) {
        ((MsgComposeScreen*)_compose)->reset(_ch_indices[_cursor]);
        _task->setCurrScreen(_compose);
      }
      return true;
    }
    if (c == KEY_SELECT || c == KEY_BACK || c == KEY_LEFT) {
      _task->gotoMsgFilter();
      return true;
    }
    return false;
  }

  uint8_t getSelectedChannelIdx() const {
    if (_cursor < _ch_count) return _ch_indices[_cursor];
    return 0;
  }
};

#endif  // UI_HAS_JOYSTICK

// ── Outdoor Settings Menu ──────────────────────────────────────────────────
// Opened by long press on the Tracking page.
// Short press (KEY_NEXT) = next entry, long press (KEY_ENTER) = execute selection
class OutdoorMenuScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _node_prefs;

  enum MenuItem { TRACKING = 0, OFFGRID, BACK, COUNT };
  uint8_t _cursor = 0;

  const char* label(MenuItem item) {
    switch (item) {
      case TRACKING: return "GPS-Share";
      case OFFGRID:  return "Off-Grid";
      case BACK:     return "Back";
      default:       return "";
    }
  }

  // Returns "[ON ]" / "[OFF]", or "" for BACK
  const char* badge(MenuItem item) {
    switch (item) {
      case TRACKING: return the_mesh.isAutoAdvertEnabled() ? "[ON ]" : "[OFF]";
      case OFFGRID:  return the_mesh.isOffGridActive()     ? "[ON ]" : "[OFF]";
      default:       return "";
    }
  }

public:
  OutdoorMenuScreen(UITask* task, NodePrefs* node_prefs)
    : _task(task), _node_prefs(node_prefs), _cursor(0) {}

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    // Titelzeile
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    display.drawTextCentered(display.width() / 2, 1, "Outdoor Settings");
    display.setColor(DisplayDriver::LIGHT);

    // Menu entries
    for (uint8_t i = 0; i < (uint8_t)COUNT; i++) {
      int y = 15 + i * 14;
      bool selected = (i == _cursor);

      if (selected) {
        display.fillRect(0, y - 1, display.width(), 12);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      // Pfeil-Cursor
      display.setCursor(0, y);
      display.print(selected ? ">" : " ");

      // Label
      display.setCursor(8, y);
      display.print(label((MenuItem)i));

      // Badge right-aligned
      const char* b = badge((MenuItem)i);
      if (b[0]) {
        int bw = display.getTextWidth(b);
        display.setCursor(display.width() - bw - 1, y);
        display.print(b);
      }

      display.setColor(DisplayDriver::LIGHT);
    }

    // Footer — only on devices without joystick (joystick operation is self-explanatory)
#if !UI_HAS_JOYSTICK
    display.setCursor(0, display.height() - 13);
    display.print("short=next  long=select");
#endif

    return 200;  // re-render every 200ms to keep badges current
  }

  bool handleInput(char c) override {
    // Short press = next entry
#if UI_HAS_JOYSTICK
    // Joystick: up/down navigates, left/right switches pages (not handled here)
    if (c == KEY_PREV) {
      _cursor = (_cursor + COUNT - 1) % COUNT;
      return true;
    }
    if (c == KEY_NEXT) {
      _cursor = (_cursor + 1) % COUNT;
      return true;
    }
#else
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _cursor = (_cursor + 1) % COUNT;
      return true;
    }
    if (c == KEY_PREV || c == KEY_LEFT) {
      _cursor = (_cursor + COUNT - 1) % COUNT;
      return true;
    }
#endif
    // Long press = execute selection
    if (c == KEY_ENTER) {
      switch ((MenuItem)_cursor) {
        case TRACKING:
          if (the_mesh.isAutoAdvertEnabled()) {
            the_mesh.setGPSAdvertEnabled(false);
            _task->notify(UIEventType::ack);
            _task->showAlert("GPS-Share: OFF", 1000);
          } else {
            the_mesh.setGPSAdvertEnabled(true);
            _task->notify(UIEventType::ack);
            _task->showAlert("GPS-Share: ON", 1000);
          }
          break;
        case OFFGRID:
          the_mesh.toggleOffGrid();
          _task->notify(UIEventType::ack);
          _task->showAlert(the_mesh.isOffGridActive() ? "Off-Grid: ON" : "Off-Grid: OFF", 1000);
          break;
        case BACK:
          _task->gotoHomeScreen();
          break;
        default:
          break;
      }
      return true;
    }
    // Back key always returns to HomeScreen
    if (c == KEY_BACK) {
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

// ── SOS Alert Screen ──────────────────────────────────────────────────────
// Displayed when a !SOS message is received.
// Buzzer loops continuously until acknowledged by long press.
class SOSAlertScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _node_prefs;
  char _from[32];
  char _text[80];
  char _node_name[32];  // V3.05: node name extracted from text

public:
  SOSAlertScreen(UITask* task, NodePrefs* node_prefs)
    : _task(task), _node_prefs(node_prefs) {
    _from[0] = 0;
    _text[0] = 0;
    _node_name[0] = 0;  // V3.05
  }

  void setAlert(const char* from, const char* text) {
    strncpy(_from, from, sizeof(_from) - 1);
    _from[sizeof(_from) - 1] = 0;
    strncpy(_text, text, sizeof(_text) - 1);
    _text[sizeof(_text) - 1] = 0;

    // V3.05: extract node name from text — format: "NodeName: !SOS ..."
    // Everything before the first ':' is the node name
    _node_name[0] = 0;
    const char* colon = strchr(text, ':');
    if (colon && colon > text) {
      int len = colon - text;
      if (len > 31) len = 31;
      strncpy(_node_name, text, len);
      _node_name[len] = 0;
      // Leerzeichen am Ende trimmen
      for (int i = len - 1; i >= 0 && _node_name[i] == ' '; i--)
        _node_name[i] = 0;
    } else {
      // Fallback: channel_name (#sos) if no ':' found
      strncpy(_node_name, from, sizeof(_node_name) - 1);
      _node_name[sizeof(_node_name) - 1] = 0;
    }
  }

  void poll() override {
#ifdef PIN_BUZZER
    if (!_task->isBuzzerPlaying()) {
      _task->playSOSAlarm();  // restart siren when finished
    }
#endif
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    // Titelzeile invertiert
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    display.drawTextCentered(display.width() / 2, 1, "!! SOS ALARM !!");
    display.setColor(DisplayDriver::LIGHT);

    if (display.height() >= 128) {
      // Large display: large icon (48×48), node name size 2 (fallback size 1 for 15+ characters)
      display.drawXbm((display.width() - 48) / 2, 16, advert_icon_large, 48, 48);

      display.setTextSize(strlen(_node_name) >= 15 ? 1 : 2);
      display.setColor(DisplayDriver::YELLOW);
      display.drawTextCentered(display.width() / 2, 72, _node_name);
      display.setTextSize(1);
    } else {
      // Kleines Display: kompaktes Layout wie bisher
      display.drawXbm((display.width() - 32) / 2, 13, advert_icon, 32, 32);

      display.setCursor(0, 47);
      display.setTextSize(1);
      display.setColor(DisplayDriver::YELLOW);
      display.print(_node_name);  // V3.05: Node-Name statt Channel-Name
    }

    // Hint at the bottom — relative on all displays
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, display.height() - 13, "long press = OK");

    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_ENTER || c == KEY_BACK) {
      // Acknowledge: stop buzzer, return to home
      _task->stopBuzzer();
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

// ── SOS Send Screen ───────────────────────────────────────────────────────
// Opened by long press on the SOS page.
// Two states: idle → confirmation → send
class SOSSendScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _node_prefs;
  bool _confirm;  // false = idle, true = confirmation

public:
  SOSSendScreen(UITask* task, NodePrefs* node_prefs)
    : _task(task), _node_prefs(node_prefs), _confirm(false) {}

  void resetState() { _confirm = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    // Titelzeile
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    display.drawTextCentered(display.width() / 2, 1, "SEND SOS");
    display.setColor(DisplayDriver::LIGHT);

    // Icon: vertically centred in the space between title and hint line
    // Large display: 48×48, small display: 32×32
    bool large_display = display.height() >= 128;
    int icon_size = large_display ? 48 : 32;
    int icon_y = display.height() / 2 - icon_size / 2;

    if (!_confirm) {
      // Zustand 1: Idle
      if (large_display)
        display.drawXbm((display.width() - 48) / 2, icon_y, advert_icon_large, 48, 48);
      else
        display.drawXbm((display.width() - 32) / 2, icon_y, advert_icon, 32, 32);
#if UI_HAS_JOYSTICK
      display.drawTextCentered(display.width() / 2, display.height() - 13, "longpress: SEND");
#else
      display.drawTextCentered(display.width() / 2, display.height() - 13, "longpress: SEND");
#endif
    } else {
      // State 2: confirmation
      if (large_display)
        display.drawXbm((display.width() - 48) / 2, icon_y, advert_icon_large, 48, 48);
      else
        display.drawXbm((display.width() - 32) / 2, icon_y, advert_icon, 32, 32);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, display.height() - 24, "SURE?");
#if UI_HAS_JOYSTICK
      display.drawTextCentered(display.width() / 2, display.height() - 13, "longpress: SEND");
#else
      display.drawTextCentered(display.width() / 2, display.height() - 13, "long:ok  short:cancel");
#endif
    }

    return 500;
  }

  bool handleInput(char c) override {
    // Back key: always return to HomeScreen (without sending)
    if (c == KEY_BACK) {
      _confirm = false;
      _task->gotoHomeScreen();
      return true;
    }
    if (!_confirm) {
      if (c == KEY_SELECT) {
        _confirm = true;
        return true;
      }
      if (c == KEY_ENTER || c == KEY_NEXT || c == KEY_PREV) {
        _confirm = false;
        _task->gotoHomeScreen();
        return true;
      }
    } else {
      if (c == KEY_SELECT) {
        _confirm = false;
        if (the_mesh.sendSOS()) {
          _task->notify(UIEventType::ack);
          _task->showAlert("SOS sent!", 2000);
        } else {
          _task->showAlert("No SOS channel!", 2000);
        }
        _task->gotoHomeScreen();
        return true;
      }
      if (c == KEY_ENTER || c == KEY_NEXT || c == KEY_PREV) {
        _confirm = false;
        return true;
      }
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;

#if ENV_INCLUDE_GPS == 1
  // Apply GPS preferences from stored prefs
  if (_sensors != NULL && _node_prefs != NULL) {
    _sensors->setSettingValue("gps", _node_prefs->gps_enabled ? "1" : "0");
    if (_node_prefs->gps_interval > 0) {
      char interval_str[12];  // Max: 24 hours = 86400 seconds (5 digits + null)
      sprintf(interval_str, "%u", _node_prefs->gps_interval);
      _sensors->setSettingValue("gps_interval", interval_str);
    }
  }
#endif

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
#endif

#ifdef DISP_BACKLIGHT
  // Backlight off at boot. Initialise PIN_BUTTON2 as toggle button.
  // The correct pin value comes from variant.h of the respective device.
  #ifdef PIN_BUTTON2
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  #endif
  pinMode(DISP_BACKLIGHT, OUTPUT);
  digitalWrite(DISP_BACKLIGHT, LOW);
  _backlight_on = false;
  #ifdef PIN_BUTTON2
  _btn2_was_pressed = false;
  #endif
  _backlight_initialized = false;
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_history = new MsgHistoryScreen(this, &rtc_clock);
  msg_filter  = new MsgFilterScreen(this, msg_history);
  fav_preview = new FavPreviewScreen(this, &rtc_clock);
  outdoor_menu = new OutdoorMenuScreen(this, node_prefs);
  sos_alert = new SOSAlertScreen(this, node_prefs);   // V3
  sos_send  = new SOSSendScreen(this, node_prefs);    // V3
#if UI_HAS_JOYSTICK
  // V5: channel_select is created first as a placeholder, then linked with compose.
  // MsgComposeScreen receives channel_select as back-reference for navigation.
  channel_select = new ChannelSelectScreen(this, NULL);  // compose-Referenz unten setzen
  msg_compose    = new MsgComposeScreen(this, channel_select);
  ((ChannelSelectScreen*)channel_select)->setCompose(msg_compose);
#endif
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
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

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    // App has fully drained the sync queue — also clear history in RAM.
    // Messages read via the app should not remain on the display.
    ((MsgHistoryScreen*) msg_history)->clearAll();  // also resets _num_unread via setUnreadCount(0)
    _latest_fav_name[0] = 0;  //resets Favorite_cache at App-Sync
    _latest_fav_time = 0;
    
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, bool is_favorite, const char* channel_name) {
  _msgcount = msgcount;

  // V3: do not interrupt SOS alarm with normal messages
  if (curr == sos_alert) return;

  // V5: Nachricht in History speichern.
  // addMessage() calls setUnreadCount(num_total) — _num_unread is set there.
  // newMsg() is called by MyMesh only when app is NOT connected (since V5.03).
  ((MsgHistoryScreen*) msg_history)->addMessage(path_len, from_name, text, is_favorite, channel_name);

  // Update online node cache — from_name is always the node name at this point
  // (channel messages: MyMesh resolves the sender name before calling newMsg)
  if (from_name && from_name[0] != 0) {
    updateOnlineNode(from_name, rtc_clock.getCurrentTime());
  }

  // Update favourite cache for HomeScreen FIRST page
  if (is_favorite) {
    strncpy(_latest_fav_name, from_name, sizeof(_latest_fav_name) - 1);
    _latest_fav_name[sizeof(_latest_fav_name) - 1] = 0;
    _latest_fav_time = rtc_clock.getCurrentTime();
  }

  // V5.05: favourite popup — always open on a favourite message.
  // Also when phone is connected (user may not be looking at the phone).
  // FavPreviewScreen always overwrites the last entry (no buffer).
  // Wegdruecken loescht nichts — Nachricht bleibt in History erhalten.
  // Exception: if history or filter screen is already active → show alert only, no popup switch.
  if (is_favorite) {
    ((FavPreviewScreen*) fav_preview)->setMessage(path_len, from_name, text, channel_name);
    if (curr != msg_history && curr != msg_filter) {
      setCurrScreen(fav_preview);
    } else {
      showAlert("+1 fav", 1500);
    }
  } else if (curr == msg_history) {
    // Non-favourite, but history screen is open: show brief alert
    showAlert("+1 new", 1500);
  }

  // Display refresh logic: identical to V4
  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
#ifdef DISP_BACKLIGHT
      digitalWrite(DISP_BACKLIGHT, _backlight_on ? HIGH : LOW);
#endif
    }
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 100;
    }
  }
}

void UITask::updateOnlineNode(const char* name, uint32_t timestamp) {
  if (!name || name[0] == 0) return;

  // already present? -> update timestamp
  for (int i = 0; i < ONLINE_CACHE_SIZE; i++) {
    if (_online_nodes[i].name[0] != 0 &&
        strncmp(_online_nodes[i].name, name, sizeof(_online_nodes[i].name) - 1) == 0) {
      _online_nodes[i].last_seen = timestamp;
      return;
    }
  }

  // new entry: find free slot
  for (int i = 0; i < ONLINE_CACHE_SIZE; i++) {
    if (_online_nodes[i].name[0] == 0) {
      strncpy(_online_nodes[i].name, name, sizeof(_online_nodes[i].name) - 1);
      _online_nodes[i].name[sizeof(_online_nodes[i].name) - 1] = 0;
      _online_nodes[i].last_seen = timestamp;
      return;
    }
  }

  // cache full: overwrite oldest entry
  int oldest_idx = 0;
  for (int i = 1; i < ONLINE_CACHE_SIZE; i++) {
    if (_online_nodes[i].last_seen < _online_nodes[oldest_idx].last_seen) {
      oldest_idx = i;
    }
  }
  strncpy(_online_nodes[oldest_idx].name, name, sizeof(_online_nodes[oldest_idx].name) - 1);
  _online_nodes[oldest_idx].name[sizeof(_online_nodes[oldest_idx].name) - 1] = 0;
  _online_nodes[oldest_idx].last_seen = timestamp;
}

int UITask::countOnlineNodes(uint32_t now, uint32_t window_secs) const {
  int count = 0;
  for (int i = 0; i < ONLINE_CACHE_SIZE; i++) {
    if (_online_nodes[i].name[0] == 0) continue;
    uint32_t age = (now >= _online_nodes[i].last_seen)
                   ? (now - _online_nodes[i].last_seen)
                   : 0;
    if (age <= window_secs) count++;
  }
  return count;
}

int UITask::countOnlineNodesExcluding(uint32_t now, uint32_t window_secs,
                                       const AdvertPath* adv_buf, int adv_count) const {
  int count = 0;
  for (int i = 0; i < ONLINE_CACHE_SIZE; i++) {
    if (_online_nodes[i].name[0] == 0) continue;
    uint32_t age = (now >= _online_nodes[i].last_seen)
                   ? (now - _online_nodes[i].last_seen) : 0;
    if (age > window_secs) continue;

    // Skip this entry if the same node is already counted via a fresh advert
    bool in_adv = false;
    for (int j = 0; j < adv_count && !in_adv; j++) {
      if (adv_buf[j].name[0] == 0) continue;
      uint32_t adv_age = (now >= adv_buf[j].recv_timestamp)
                         ? (now - adv_buf[j].recv_timestamp) : 0;
      if (adv_age > window_secs) continue;
      if (strncmp(_online_nodes[i].name, adv_buf[j].name,
                  sizeof(_online_nodes[i].name) - 1) == 0) {
        in_adv = true;
      }
    }
    if (!in_adv) count++;
  }
  return count;
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

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
    _display->turnOff();
    radio_driver.powerOff();
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);       // short = confirm selection (in menus)
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);      // long = context action (open menu, SOS etc.)
  }
  ev = joystick_up.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_PREV);        // up = previous entry in menu
  }
  ev = joystick_down.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);        // down = next entry in menu
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);        // links = vorherige Seite
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);       // right = next page
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_BACK);       // einfacher Druck = Zurueck-Navigation
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);  // 3x = Buzzer toggle (unveraendert)
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  // On first loop() call ensure backlight is really off.
  // (GxEPD turnOn() may have turned it on during boot)
  // This block is only active when BACKLIGHT_BTN is not defined —
  // devices with BACKLIGHT_BTN use the block above.
  if (!_backlight_initialized) {
    digitalWrite(DISP_BACKLIGHT, LOW);
    _backlight_initialized = true;
  }

  // PIN_BUTTON2 as backlight toggle — only if the button is present on the device.
  // INPUT_PULLUP: LOW = gedrueckt, HIGH = losgelassen
  #ifdef PIN_BUTTON2
  bool btn2_state = (digitalRead(PIN_BUTTON2) == LOW);
  if (btn2_state && !_btn2_was_pressed) {
    _backlight_on = !_backlight_on;
    digitalWrite(DISP_BACKLIGHT, _backlight_on ? HIGH : LOW);
  }
  _btn2_was_pressed = btn2_state;
  #endif

#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {

      // show low battery shutdown alert
      // we should only do this for eink displays, which will persist after power loss
      #if defined(THINKNODE_M1) || defined(LILYGO_TECHO)
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::RED);
        _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
        _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
        _display->endFrame();
      }
      #endif

      shutdown();

    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

#if UI_HAS_JOYSTICK
// V5: load channels and switch to ChannelSelectScreen.
// Implemented here (not inline in .h) because ChannelSelectScreen is not known in the header.
void UITask::gotoChannelSelect() {
  ((ChannelSelectScreen*)channel_select)->loadChannels();
  setCurrScreen(channel_select);
}
#endif

void UITask::refreshDisplay() {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();
#ifdef DISP_BACKLIGHT
      digitalWrite(DISP_BACKLIGHT, _backlight_on ? HIGH : LOW);
#endif
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 100;
  }
}

bool UITask::isOnRecentOrTrackingPage() const {
  if (curr == home && home != NULL) {
    HomeScreen* h = (HomeScreen*)home;
    return h->isOnRecentPage() || h->isOnTrackingPage();
  }
  return false;
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();
#ifdef DISP_BACKLIGHT
      digitalWrite(DISP_BACKLIGHT, _backlight_on ? HIGH : LOW);
#endif
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  } else if (curr == outdoor_menu) {
    // Long press in menu = execute selection
    curr->handleInput(KEY_ENTER);
    c = 0;
  } else if (curr == msg_filter) {
    // V5: long press in MsgFilter = execute selection (M1: no joystick)
    curr->handleInput(KEY_ENTER);
    c = 0;
  } else if (curr == msg_history) {
    // V5: long press in history = delete all messages
    // KEY_SELECT = explizit "lang" — kurzer Druck (KEY_ENTER) tut in History nichts
    curr->handleInput(KEY_SELECT);
    c = 0;
  } else if (curr == fav_preview) {
    // V5.05: Langer Druck im Favoriten-Popup tut bewusst nichts
    c = 0;
  } else if (curr == sos_alert) {
    // Long press on SOS alarm = acknowledge
    curr->handleInput(KEY_ENTER);
    c = 0;
  } else if (curr == sos_send) {
    // Langer Druck im SOS-Sende-Screen:
    // KEY_SELECT = explicitly "long" so handleInput can distinguish short/long
    curr->handleInput(KEY_SELECT);
    c = 0;
  } else if (curr == home && ((HomeScreen*)home)->isOnFirstPage()) {
    // V5: long press on FIRST page (message page) = open MsgFilterScreen (M1)
    setCurrScreen(msg_filter);
    _next_refresh = 0;  // sofortiger Refresh
    c = 0;
  } else if (curr == home && ((HomeScreen*)home)->isOnTrackingPage()) {
    // Long press on Tracking page = open outdoor menu
    setCurrScreen(outdoor_menu);
    c = 0;
  } else if (curr == home && ((HomeScreen*)home)->isOnRecentPage()) {
    // Long press on Recent page = send advert
    // advert() entscheidet automatisch: ZeroHop (normal) oder Flood (Off-Grid)
    the_mesh.advert();
    notify(UIEventType::ack);
    showAlert("Advert sent!", 1500);
    _next_refresh = 0;  // sofortiger Refresh damit Alert gleich erscheint
    c = 0;
  } else if (curr == home && ((HomeScreen*)home)->isOnSOSPage()) {
    // V3: long press on SOS page = open SOS send screen
    ((SOSSendScreen*)sos_send)->resetState();
    setCurrScreen(sos_send);
    c = 0;
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  } 
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}

// V3: trigger SOS alarm — called by MyMesh::onChannelMessageRecv()
void UITask::triggerSOS(const char* from, const char* text) {
  ((SOSAlertScreen*)sos_alert)->setAlert(from, text);

  // Turn on display — always, even when phone is connected
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
#ifdef DISP_BACKLIGHT
    digitalWrite(DISP_BACKLIGHT, _backlight_on ? HIGH : LOW);
#endif
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;

  // Enable buzzer (overrides quiet mode — SOS is an emergency)
#ifdef PIN_BUZZER
  buzzer.quiet(false);
  _node_prefs->buzzer_quiet = 0;
  // Do NOT save prefs — buzzer_quiet stays ON after acknowledgement
  // Users who want it off can disable it manually with a triple-click
  buzzer.play("siren:d=8,o=5,b=100:d,e,d,e,d,e,d,e");
#endif

  setCurrScreen(sos_alert);
  _next_refresh = 0;
}

// V3: buzzer helper functions for SOSAlertScreen::poll()
bool UITask::isBuzzerPlaying() {
#ifdef PIN_BUZZER
  return buzzer.isPlaying();
#else
  return false;
#endif
}

void UITask::playSOSAlarm() {
#ifdef PIN_BUZZER
  buzzer.play("siren:d=8,o=5,b=100:d,e,d,e,d,e,d,e");
#endif
}

void UITask::stopBuzzer() {
#ifdef PIN_BUZZER
  buzzer.shutdown();
#endif
}
