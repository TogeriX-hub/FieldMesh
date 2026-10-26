// FieldMesh repeater additions (Station G2 Off-Grid box) - MyMesh member functions.
// Only compiled in with -D FIELDMESH_REPEATER=1.

#include "MyMesh.h"

#ifdef FIELDMESH_REPEATER

void MyMesh::fmBegin() {
  if (_fs->exists(FM_REPEATER_PREFS_FILE)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(FM_REPEATER_PREFS_FILE, "r");
#else
    File file = _fs->open(FM_REPEATER_PREFS_FILE);
#endif
    if (file) {
      _fm.loadSerial(file);
      file.close();
    }
  }
  // The radio params themselves are part of the upstream prefs (/prefs.json),
  // so an active Off-Grid mode survives a reboot without extra work here.
}

bool MyMesh::fmSavePrefs() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(FM_REPEATER_PREFS_FILE);
  File file = _fs->open(FM_REPEATER_PREFS_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(FM_REPEATER_PREFS_FILE, "w");
#else
  File file = _fs->open(FM_REPEATER_PREFS_FILE, "w", true);
#endif
  if (file) {
    bool success = _fm.saveSerial(file);
    file.close();
    return success;
  }
  return false;
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
  return false;
}

#endif
