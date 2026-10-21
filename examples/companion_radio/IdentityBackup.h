#pragma once

// FieldMesh v6.1: backup of identity and prefs on the secondary file system.
//
// Background: on some V5.x -> V6.0 updates the internal file system (InternalFS) was wiped
// completely - new identity, default settings - while contacts and channels on the secondary
// file system survived (see RECHERCHE_V6_Upgrade_Identity_Reset.md). The cause is not known.
// This keeps a copy of /_main.id and /prefs.json on the secondary file system
// (M1/L1: QSPI flash, T1000-E: ExtraFS in internal flash) and restores it at boot when the
// primary copy is missing. A factory reset formats both file systems, so it still resets.
//
// Used by DataStore only, header-only on purpose (no extra build_src_filter entries needed).

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)

#include <Arduino.h>
#include <helpers/IdentityStore.h>   // FILESYSTEM

#define FM_ID_FILE          "/_main.id"
#define FM_PREFS_FILE       "/prefs.json"
#define FM_PREFS_OLD_FILE   "/new_prefs"
#define FM_ID_BACKUP        "/fm_main.id"
#define FM_PREFS_BACKUP     "/fm_prefs.json"
#define FM_BACKUP_TMP       "/fm_backup.tmp"
#define FM_ID_FILE_SIZE     96      // pub_key (32) + prv_key (64)

class IdentityBackup {
  static uint8_t* buf_a() { static uint8_t b[64]; return b; }
  static uint8_t* buf_b() { static uint8_t b[64]; return b; }

  static size_t fileSize(FILESYSTEM* fs, const char* path) {
    if (!fs->exists(path)) return 0;
    File f = fs->open(path, FILE_O_READ);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
  }

  // copy via temp file + rename, so an interrupted copy never leaves a half-written target
  static bool copyFile(FILESYSTEM* from, const char* src, FILESYSTEM* to, const char* dst) {
    File in = from->open(src, FILE_O_READ);
    if (!in) return false;
    size_t expected = in.size();
    to->remove(FM_BACKUP_TMP);
    File out = to->open(FM_BACKUP_TMP, FILE_O_WRITE);
    if (!out) { in.close(); return false; }
    size_t total = 0;
    bool ok = true;
    uint8_t* b = buf_a();
    while (ok) {
      int n = in.read(b, 64);
      if (n <= 0) break;
      ok = (out.write(b, n) == (size_t)n);
      total += n;
    }
    in.close();
    out.close();
    if (!ok || total != expected || total == 0) {
      to->remove(FM_BACKUP_TMP);
      return false;
    }
    return to->rename(FM_BACKUP_TMP, dst);
  }

  static bool sameContent(FILESYSTEM* fa, const char* pa, FILESYSTEM* fb, const char* pb) {
    File a = fa->open(pa, FILE_O_READ);
    if (!a) return false;
    File b = fb->open(pb, FILE_O_READ);
    if (!b) { a.close(); return false; }
    bool same = (a.size() == b.size());
    uint8_t* x = buf_a();
    uint8_t* y = buf_b();
    while (same) {
      int na = a.read(x, 64);
      int nb = b.read(y, 64);
      if (na != nb) { same = false; break; }
      if (na <= 0) break;
      same = (memcmp(x, y, na) == 0);
    }
    a.close();
    b.close();
    return same;
  }

  // refresh the backup if it is missing or differs from the primary file
  static void syncOne(FILESYSTEM* primary, const char* src, FILESYSTEM* secondary, const char* dst) {
    if (!primary->exists(src)) return;
    if (secondary->exists(dst) && sameContent(primary, src, secondary, dst)) return;
    bool ok = copyFile(primary, src, secondary, dst);
    MESH_DEBUG_PRINTLN("IdentityBackup: backup %s -> %s %s", src, dst, ok ? "OK" : "FAILED");
  }

public:
  // At boot, before identity and prefs are loaded: restore missing files, then refresh backups
  // (also creates the first backup on devices updated from v6.0).
  static void begin(FILESYSTEM* primary, FILESYSTEM* secondary) {
    if (primary == nullptr || secondary == nullptr) return;

    if (!primary->exists(FM_ID_FILE) && fileSize(secondary, FM_ID_BACKUP) == FM_ID_FILE_SIZE) {
      bool ok = copyFile(secondary, FM_ID_BACKUP, primary, FM_ID_FILE);
      MESH_DEBUG_PRINTLN("IdentityBackup: identity restored %s", ok ? "OK" : "FAILED");
    }
    if (!primary->exists(FM_PREFS_FILE) && !primary->exists(FM_PREFS_OLD_FILE)
        && fileSize(secondary, FM_PREFS_BACKUP) > 0) {
      bool ok = copyFile(secondary, FM_PREFS_BACKUP, primary, FM_PREFS_FILE);
      MESH_DEBUG_PRINTLN("IdentityBackup: prefs restored %s", ok ? "OK" : "FAILED");
    }

    syncOne(primary, FM_ID_FILE, secondary, FM_ID_BACKUP);
    syncOne(primary, FM_PREFS_FILE, secondary, FM_PREFS_BACKUP);
  }

  // after the identity was written to the primary file system
  static void identitySaved(FILESYSTEM* primary, FILESYSTEM* secondary) {
    if (primary == nullptr || secondary == nullptr) return;
    syncOne(primary, FM_ID_FILE, secondary, FM_ID_BACKUP);
  }

  // after the prefs were written to the primary file system
  static void prefsSaved(FILESYSTEM* primary, FILESYSTEM* secondary) {
    if (primary == nullptr || secondary == nullptr) return;
    syncOne(primary, FM_PREFS_FILE, secondary, FM_PREFS_BACKUP);
  }
};

#endif
