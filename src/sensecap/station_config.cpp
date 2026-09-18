#include "station_config.h"

StationConfig &station_config() {
  // TODO(onboarding): replace with real provisioning (load from NVS,
  // filled in by a setup flow) — see station_config.h.
  //
  // Field-by-field assignment, not brace-init: a default member initializer
  // (dispatchNodeNum's "= 0") makes this compiler treat the struct as
  // non-aggregate, so a `StationConfig{...}` list would fail to compile
  // (same issue as CtxSlot in ui_model.cpp).
  static StationConfig cfg;
  cfg.stationId = "STATION-01";
  cfg.operatorName = "Unbekannter Betreiber";
  cfg.localContact = "Kein lokaler Kontakt hinterlegt";
  return cfg;
}
