#pragma once
// FieldMesh version (#17), shared by the companion and the FieldMesh repeater.
//
// FIELDMESH_VERSION is set at build time by the FieldMesh workflows:
//   FieldMesh Release     -D FIELDMESH_VERSION="v2.2.0"        (the version entered when starting it)
//   FieldMesh Build Check -D FIELDMESH_VERSION="dev-<commit>"
// Test builds for hardware tests set e.g. "v2.2.0-test1"; local builds without the flag show "dev".
// FIRMWARE_VERSION stays the MeshCore version of the upstream files (e.g. "v1.18.0").
//
// Plain C/C++ without Arduino, so the string rules can be tested on a PC.

#include <stddef.h>
#include <string.h>

#ifndef FIELDMESH_VERSION
#define FIELDMESH_VERSION "dev"
#endif

// Size of the firmware version field the app gets (CMD_DEVICE_QUERY, 20 bytes incl. the 0).
#define FM_APP_VERSION_FIELD 20

// MeshCore version without a "-<commit>" suffix (upstream build.sh adds one): "v1.18.0-abc123" -> "v1.18.0".
static inline void fmMeshCoreVersion(char* out, size_t sz, const char* meshcore_ver) {
  if (sz == 0) return;
  const char* dash = strchr(meshcore_ver, '-');
  size_t n = dash ? (size_t)(dash - meshcore_ver) : strlen(meshcore_ver);
  if (n >= sz) n = sz - 1;
  memcpy(out, meshcore_ver, n);
  out[n] = 0;
}

// Version text for the app and the CLI 'ver': MeshCore version + "-fm" + FieldMesh version
// without its leading 'v', e.g. "v1.18.0-fm2.2.0". Same shape as official MeshCore builds
// ("v1.18.0-<commit>"); the app takes features from FIRMWARE_VER_CODE, not from this text.
// Cut to fit the app field (19 characters): test and dev builds may lose their last characters.
static inline void fmAppVersionText(char* out, size_t sz, const char* meshcore_ver, const char* fm_ver) {
  if (sz == 0) return;
  char mc[16];
  fmMeshCoreVersion(mc, sizeof(mc), meshcore_ver);
  const char* fm = fm_ver;
  if (fm[0] == 'v' || fm[0] == 'V') fm++;
  size_t limit = sz < FM_APP_VERSION_FIELD ? sz : FM_APP_VERSION_FIELD;
  size_t pos = 0;
  const char* parts[3] = { mc, "-fm", fm };
  for (int p = 0; p < 3; p++) {
    for (const char* c = parts[p]; *c && pos + 1 < limit; c++) out[pos++] = *c;
  }
  out[pos] = 0;
}

// Same with the versions of this build, kept in a static buffer.
static inline const char* fmAppVersion(const char* meshcore_ver) {
  static char buf[FM_APP_VERSION_FIELD];
  if (buf[0] == 0) fmAppVersionText(buf, sizeof(buf), meshcore_ver, FIELDMESH_VERSION);
  return buf;
}
