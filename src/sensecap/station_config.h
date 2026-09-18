#pragma once
#include <Arduino.h>

// Local operator / station provisioning data. Hardcoded for now — this is
// deliberately its own small module (not scattered constants in ui_model.cpp)
// so a future onboarding flow has one obvious place to write real values
// into: whoever sets up a new station without prior context needs to fill
// in exactly these fields (station ID, who runs it, who to call if
// something goes wrong) before the terminal is really ready for field use.
struct StationConfig {
  String stationId;
  String operatorName;  // "lokaler Betreiber"
  String localContact;  // who/what to call if a transmission keeps failing

  // Meshtastic node number of the "Leitstelle" (dispatch) that emergency
  // reports are sent to as a direct message (so we can get a real
  // delivery ACK -- Meshtastic never acks broadcasts, see
  // meshtastic_proto.cpp). 0 = not configured yet; set via the
  // "dispatch set <hex-node-id>" serial command until there's a real
  // onboarding UI for it.
  uint32_t dispatchNodeNum = 0;
};

// Single shared instance. A later onboarding flow would load/save this
// from/to flash (e.g. Preferences/NVS) instead of returning a hardcoded
// struct — callers should go through this accessor either way so that
// swap-in doesn't touch call sites.
StationConfig &station_config();
