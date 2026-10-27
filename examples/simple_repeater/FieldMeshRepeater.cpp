// FieldMesh repeater additions (Station G2 Off-Grid box) - MyMesh member functions.
// Only compiled in with -D FIELDMESH_REPEATER=1.

#include "MyMesh.h"
#include "FieldMeshFs.h"

#ifdef FIELDMESH_REPEATER

void MyMesh::fmBegin() {
  fmClockRestore();   // before anything gets a timestamp
  _fm_boot_reason = board.getResetReasonString(board.getResetReason());   // for the dashboard

  if (fmFsExists(_fs, FM_REPEATER_PREFS_FILE)) {
    File file = fmFsOpenRead(_fs, FM_REPEATER_PREFS_FILE);
    if (file) {
      _fm.loadSerial(file);
      file.close();
    }
  }
  // The radio params themselves are part of the upstream prefs (/prefs.json),
  // so an active Off-Grid mode survives a reboot without extra work here.

  if (_fm.room_enabled && !fmRoomLoadIdentity(false)) {
    _fm.room_enabled = 0;   // identity file missing (e.g. erased): room stays off until 'set roomserver'
  }
  fmRoomBootPost();   // "Box started (...)" on the board, if the room is on
  fmChannelsLoad();   // channels the box listens to (SOS)
  fmStatsBegin();     // dashboard data: node table, team
  fmWxLoad();         // last weather forecast
  if (_fm.wifi_enabled) fmWebStart();   // WiFi dashboard (FieldMeshWeb.cpp)
}

bool MyMesh::fmSavePrefs() {
  File file = fmFsOpenWriteTmp(_fs, FM_REPEATER_PREFS_FILE);
  if (file) {
    bool success = _fm.saveSerial(file);
    file.close();
    return success && fmFsCommit(_fs, FM_REPEATER_PREFS_FILE);
  }
  return false;
}

// ---- clock high-water mark ----
// The G2 has no battery-backed clock: after a power loss it restarts on an old date.
// Peers ignore adverts that are older than the last one they heard from us (new name,
// position ...), and room posts must stay in time order. So the current time is saved
// every few minutes, and after a restart the clock is never behind the saved value.

void MyMesh::fmClockRestore() {
  _fm_clock_saved = 0;
  if (!fmFsExists(_fs, FM_CLOCK_FILE)) return;
  File f = fmFsOpenRead(_fs, FM_CLOCK_FILE);
  if (!f) return;
  uint8_t hdr[3];
  uint32_t t;
  if (f.read(hdr, 3) == 3 && hdr[0] == 'F' && hdr[1] == 'T' && hdr[2] == 1 && f.read((uint8_t *)&t, 4) == 4) {
    _fm_clock_saved = t;
    if (getRTCClock()->getCurrentTime() <= t) {
      // adverts sent up to FM_CLOCK_SAVE_INTERVAL after the save may be a little newer:
      // at worst they are ignored by peers for that long, the clock never runs ahead
      getRTCClock()->setCurrentTime(t + 1);
    }
  }
  f.close();
}

void MyMesh::fmClockSaveLoop() {
  if (_fm_clock_next_save && !millisHasNowPassed(_fm_clock_next_save)) return;
  _fm_clock_next_save = futureMillis(FM_CLOCK_SAVE_INTERVAL);
  uint32_t now = getRTCClock()->getCurrentTime();
  if (now <= _fm_clock_saved) return;   // nothing new (clock not moving forward)
  File f = fmFsOpenWriteTmp(_fs, FM_CLOCK_FILE);
  if (!f) return;
  uint8_t hdr[3] = { 'F', 'T', 1 };
  bool ok = f.write(hdr, 3) == 3 && f.write((uint8_t *)&now, 4) == 4;
  f.close();
  if (ok && fmFsCommit(_fs, FM_CLOCK_FILE)) _fm_clock_saved = now;
}

void MyMesh::fmSetOffGrid(bool on, char* reply) {
  char tmp[64];

  if (on == (_fm.offgrid != 0)) {
    snprintf(tmp, sizeof(tmp), "Off-Grid already %s", on ? "ON" : "OFF");
  } else {
    if (on) {
      // remember the normal radio params, then switch to Off-Grid
      _fm.normal_freq = _prefs.freq;
      _fm.normal_bw   = _prefs.bw;
      _fm.normal_sf   = _prefs.sf;
      _fm.normal_cr   = _prefs.cr;
      _prefs.freq = offGridFreqFor(_fm.normal_freq);   // EU < 900 MHz <= US
      _prefs.bw   = OFFGRID_BW;
      _prefs.sf   = OFFGRID_SF;
      _prefs.cr   = OFFGRID_CR;
      _fm.offgrid = 1;
      snprintf(tmp, sizeof(tmp), "OK - Off-Grid ON (%s %.4f MHz)", offGridRegionLabel(_prefs.freq), _prefs.freq);
    } else {
      if (_fm.normal_freq > 0.0f) {   // never switched on before: keep current params
        _prefs.freq = _fm.normal_freq;
        _prefs.bw   = _fm.normal_bw;
        _prefs.sf   = _fm.normal_sf;
        _prefs.cr   = _fm.normal_cr;
      }
      _fm.offgrid = 0;
      snprintf(tmp, sizeof(tmp), "OK - Off-Grid OFF (%.3f MHz)", _prefs.freq);
    }
    savePrefs();
    fmSavePrefs();

    // Apply live (no reboot) through the existing pending-params path in loop(),
    // delayed so a remote CLI reply still goes out on the old params.
    // TX power is never touched.
    pending_freq = _prefs.freq;
    pending_bw   = _prefs.bw;
    pending_sf   = _prefs.sf;
    pending_cr   = _prefs.cr;
    revert_radio_at = 0;   // cancel a running 'tempradio'
    set_radio_at = futureMillis(2000);
    _fm_advert_after_radio = true;
  }
  if (reply) strcpy(reply, tmp);
}

bool MyMesh::fmHandleCommand(uint32_t sender_timestamp, const char* command, char* reply) {
  (void)sender_timestamp;   // allowed locally (serial) and remotely (admin login)

  if (strcmp(command, "set offgrid on") == 0) {
    fmSetOffGrid(true, reply);
    return true;
  }
  if (strcmp(command, "set offgrid off") == 0) {
    fmSetOffGrid(false, reply);
    return true;
  }
  if (memcmp(command, "set offgrid", 11) == 0) {
    strcpy(reply, "Err - use: set offgrid on|off");
    return true;
  }
  if (strcmp(command, "get offgrid") == 0) {
    sprintf(reply, "> %s (%.4f MHz)", _fm.offgrid ? "on" : "off", _prefs.freq);
    return true;
  }

  // Room server (second identity, FieldMeshRoom.cpp)
  if (memcmp(command, "set roomserver", 14) == 0 && (command[14] == 0 || command[14] == ' ')) {
    char name[32];
    const char* sp = &command[14];
    while (*sp == ' ') sp++;
    StrHelper::strncpy(name, sp, sizeof(name));
    int n = strlen(name);
    while (n > 0 && name[n-1] == ' ') name[--n] = 0;   // trim trailing spaces

    if (n == 0) {
      strcpy(reply, "Err - use: set roomserver <name>|on|off");
    } else if (strcmp(name, "off") == 0) {
      fmSetRoom(false, NULL, reply);
    } else if (strcmp(name, "on") == 0) {
      fmSetRoom(true, NULL, reply);   // last name, or the repeater name
    } else {
      fmSetRoom(true, name, reply);
    }
    return true;
  }
  if (strcmp(command, "get roomserver") == 0) {
    if (fmRoomActive()) {
      sprintf(reply, "> on: %s, %d clients, %d posts", fmRoomName(), _room_acl.getNumClients(), fmRoomNumPosts());
    } else {
      strcpy(reply, "> off");
    }
    return true;
  }
  if (memcmp(command, "set room.pass", 13) == 0 && (command[13] == 0 || command[13] == ' ')) {
    const char* pw = &command[13];
    while (*pw == ' ') pw++;
    if (strlen(pw) >= sizeof(_fm.room_pass)) {
      sprintf(reply, "Err - max %d chars", (int)sizeof(_fm.room_pass) - 1);
    } else {
      StrHelper::strncpy(_fm.room_pass, pw, sizeof(_fm.room_pass));
      fmSavePrefs();
      fmRoomClearUsers();   // only those who know the new password get back in
      strcpy(reply, _fm.room_pass[0] ? "OK - room password set, all logged out" : "OK - room open (no password), all logged out");
    }
    return true;
  }
  if (strcmp(command, "get room.pass") == 0) {
    sprintf(reply, "> %s", _fm.room_pass[0] ? _fm.room_pass : "(none)");
    return true;
  }
  if (fmChannelsHandleCommand(command, reply)) return true;   // channel.add / channel.del / get channels / get sos
  if (fmStatsHandleCommand(command, reply)) return true;      // team.add / team.del / get team / get nodes
  if (fmWxHandleCommand(command, reply)) return true;         // set weather.loc / get weather

  if (strcmp(command, "set wifi on") == 0) {
    fmSetWifi(true, reply);
    return true;
  }
  if (strcmp(command, "set wifi off") == 0) {
    fmSetWifi(false, reply);
    return true;
  }
  if (memcmp(command, "set wifi", 8) == 0) {
    strcpy(reply, "Err - use: set wifi on|off");
    return true;
  }
  if (strcmp(command, "get wifi") == 0) {
    if (fmWebIsOn()) {
      snprintf(reply, 150, "> on: '%s', http://%s.local (QR: 192.168.4.1), %d phones", fmWebCurrentSsid(), fmWebCurrentHost(), fmWebStations());
    } else {
      strcpy(reply, "> off");
    }
    return true;
  }
  if (memcmp(command, "start ota", 9) == 0 && fmWebIsOn()) {
    // WiFi dashboard is running: add the update page to it instead of the upstream
    // "MeshCore-OTA" access point (switching networks while the dashboard runs failed)
    fmWebStartOta(reply);
    return true;
  }
  if (memcmp(command, "start ota", 9) == 0) {
    fmWebNoteUpstreamOta();   // upstream OTA owns WiFi + port 80 now: dashboard stays off until restart
    return false;             // upstream CLI starts the OTA update
  }

  if (strcmp(command, "room.reset") == 0) {   // same as the menu: posts, users, password; name stays
    fmRoomReset();
    strcpy(reply, "OK - room reset (posts, users, password); name stays");
    return true;
  }
  if (strcmp(command, "room.delete") == 0) {   // everything back to the start: room off, next 'on' gets a new key
    fmRoomClearPosts();
    fmRoomClearUsers();
    _fm.room_pass[0] = 0;
    _fm.room_name[0] = 0;
    _fm.room_enabled = 0;
    fmSavePrefs();
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    const char* dir = "";
#else
    const char* dir = "/identity";   // IdentityStore, as in fmRoomLoadIdentity()
#endif
    char fn[48];
    snprintf(fn, sizeof(fn), "%s/%s.id", dir, FM_ROOM_IDENTITY_NAME);
    if (_fs->exists(fn)) _fs->remove(fn);
    _room_id_loaded = false;
    strcpy(reply, "OK - room deleted (posts, users, password, name, key) and OFF");
    return true;
  }
  if (memcmp(command, "room.clear", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
    const char* what = &command[10];
    while (*what == ' ') what++;
    if (strcmp(what, "posts") == 0) {
      fmRoomClearPosts();
      strcpy(reply, "OK - all posts deleted");
    } else if (strcmp(what, "users") == 0) {
      fmRoomClearUsers();
      strcpy(reply, "OK - all users logged out");
    } else if (strcmp(what, "pass") == 0) {
      _fm.room_pass[0] = 0;
      fmSavePrefs();
      fmRoomClearUsers();   // same rule as 'set room.pass'
      strcpy(reply, "OK - room open (no password), all logged out");
    } else if (strcmp(what, "name") == 0) {
      _fm.room_name[0] = 0;   // back to the repeater name
      fmSavePrefs();
      fmRoomSendAdvert(false, 500);   // apps pick up the new name (only if the room is on)
      fmRoomSendAdvert(true, 4000);
      sprintf(reply, "OK - room name is now '%s'", fmRoomName());
    } else {
      strcpy(reply, "Err - use: room.clear posts|users|pass|name  (everything: room.delete)");
    }
    return true;
  }
  return false;
}

#endif
