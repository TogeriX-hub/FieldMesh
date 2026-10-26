#pragma once

// FieldMesh repeater additions (Station G2 Off-Grid box).
// Everything here is only active with -D FIELDMESH_REPEATER=1, so all other
// repeater builds keep the upstream behaviour.

#ifdef FIELDMESH_REPEATER

#include <Arduino.h>
#include <helpers/ConfigSerializer.h>
#include "../companion_radio/OffGrid.h"   // shared Off-Grid parameters (EU/US chosen from the normal frequency)

#define FM_REPEATER_PREFS_FILE  "/fm_repeater.json"

// FieldMesh settings live in their own file, so the upstream /prefs.json stays untouched.
class FieldMeshRepeaterPrefs : public ConfigSerializer {
public:
  uint8_t offgrid = 0;          // 1 = Off-Grid radio params active
  float   normal_freq = 0;      // radio params saved before switching to Off-Grid
  float   normal_bw = 0;
  uint8_t normal_sf = 0;
  uint8_t normal_cr = 0;

protected:
  void structure() override {
    def("offgrid", offgrid);
    def("n_freq", normal_freq);
    def("n_bw", normal_bw);
    def("n_sf", normal_sf);
    def("n_cr", normal_cr);
  }
};

// What the display/menu needs from the mesh, without knowing MyMesh.
class FieldMeshUIHost {
public:
  virtual bool fmIsOffGrid() const = 0;
  virtual void fmSetOffGrid(bool on, char* reply) = 0;   // reply: short status text (may be NULL)
};

#endif
