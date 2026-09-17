#include "station_config.h"

StationConfig &station_config() {
  // TODO(onboarding): replace with real provisioning (load from NVS,
  // filled in by a setup flow) — see station_config.h.
  static StationConfig cfg{
      "STATION-01",
      "Unbekannter Betreiber",
      "Kein lokaler Kontakt hinterlegt",
  };
  return cfg;
}
