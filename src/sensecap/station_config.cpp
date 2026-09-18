#include "station_config.h"
#include <Preferences.h>

namespace {
const char *kPrefsNamespace = "kaynafunkt";
const char *kDispatchKey = "dispatch";
bool g_loaded = false;
} // namespace

StationConfig &station_config() {
  // TODO(onboarding): replace with real provisioning (load from NVS,
  // filled in by a setup flow) — see station_config.h.
  //
  // Field-by-field assignment, not brace-init: a default member initializer
  // (dispatchNodeNum's "= 0") makes this compiler treat the struct as
  // non-aggregate, so a `StationConfig{...}` list would fail to compile
  // (same issue as CtxSlot in ui_model.cpp).
  static StationConfig cfg;
  if (!g_loaded) {
    cfg.stationId = "STATION-01";
    cfg.operatorName = "Unbekannter Betreiber";
    cfg.localContact = "Kein lokaler Kontakt hinterlegt";

    Preferences prefs;
    prefs.begin(kPrefsNamespace, /*readOnly=*/true);
    cfg.dispatchNodeNum = prefs.getUInt(kDispatchKey, 0);
    prefs.end();
    g_loaded = true;
  }
  return cfg;
}

void station_config_set_dispatch_node(uint32_t nodeNum) {
  station_config().dispatchNodeNum = nodeNum; // ensures cfg is loaded first

  Preferences prefs;
  prefs.begin(kPrefsNamespace, /*readOnly=*/false);
  prefs.putUInt(kDispatchKey, nodeNum);
  prefs.end();
}
