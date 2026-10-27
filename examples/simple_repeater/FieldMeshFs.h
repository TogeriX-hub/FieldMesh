#pragma once

// FieldMesh repeater: small file helpers for the FieldMesh files.
// Writing goes to "<name>.tmp" first and replaces the file only when the new one is complete,
// so a power loss while saving leaves either the old file or the new one, never a half file.
// Only used with -D FIELDMESH_REPEATER=1.

#ifdef FIELDMESH_REPEATER

#include <helpers/IdentityStore.h>   // FILESYSTEM

static inline void fmFsTmpName(char* dest, size_t sz, const char* name) {
  snprintf(dest, sz, "%s.tmp", name);
}

// true if the file exists. A power loss between removing the old file and renaming the new
// one leaves only "<name>.tmp" (complete at that point): finish the rename.
static inline bool fmFsExists(FILESYSTEM* fs, const char* name) {
  if (fs->exists(name)) return true;
  char tmp[40];
  fmFsTmpName(tmp, sizeof(tmp), name);
  if (fs->exists(tmp)) {
    fs->rename(tmp, name);
    return fs->exists(name);
  }
  return false;
}

static inline File fmFsOpenRead(FILESYSTEM* fs, const char* name) {
#if defined(RP2040_PLATFORM)
  return fs->open(name, "r");
#else
  return fs->open(name);
#endif
}

static inline File fmFsOpenWriteTmp(FILESYSTEM* fs, const char* name) {
  char tmp[40];
  fmFsTmpName(tmp, sizeof(tmp), name);
  fs->remove(tmp);   // leftover of an interrupted write
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(tmp, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(tmp, "w");
#else
  return fs->open(tmp, "w", true);
#endif
}

// call after closing the file from fmFsOpenWriteTmp(): replaces <name> with the new content
static inline bool fmFsCommit(FILESYSTEM* fs, const char* name) {
  char tmp[40];
  fmFsTmpName(tmp, sizeof(tmp), name);
  fs->remove(name);
  return fs->rename(tmp, name);
}

#endif
