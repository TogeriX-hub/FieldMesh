#pragma once

// FieldMesh: Off-Grid radio parameters.
// The Off-Grid frequency is chosen automatically from the normal (pre Off-Grid) frequency,
// so one firmware per device serves both regions:
//   below 900 MHz  -> EU 869.4625 MHz (SRD sub-band g1, 25 mW, 10% duty cycle, no LBT)
//   900 MHz and up -> US 916.4625 MHz (915 MHz ISM band)
// India (865-867 MHz) maps to EU and Japan/Korea (~920-923 MHz) to US - not compliant there,
// accepted on purpose (see README).

#define OFFGRID_FREQ_EU     869.4625f
#define OFFGRID_FREQ_US     916.4625f
#define OFFGRID_REGION_SPLIT_MHZ  900.0f
#define OFFGRID_BW          125.0f
#define OFFGRID_SF          11
#define OFFGRID_CR          5

static inline bool offGridIsUS(float normal_freq) {
  return normal_freq >= OFFGRID_REGION_SPLIT_MHZ;
}

static inline float offGridFreqFor(float normal_freq) {
  return offGridIsUS(normal_freq) ? OFFGRID_FREQ_US : OFFGRID_FREQ_EU;
}

// region label of an active Off-Grid frequency ("EU" / "US")
static inline const char* offGridRegionLabel(float offgrid_freq) {
  return offGridIsUS(offgrid_freq) ? "US" : "EU";
}
