#pragma once
#include <Arduino.h>

// Bridges to an external Meshtastic node (stock firmware, e.g. the Seeed
// XIAO nRF52 already used by src/xiao/main.cpp) over UART — Weg 2 from
// ui/README.md: this board's own onboard LoRa is deliberately NOT used
// yet (see platformio.ini's sensecap_indicator env comment). Same
// approach as the XIAO board, just on different GPIOs (22 TX / 23 RX
// instead of D6/D7): those are free here, while 43/44 are this board's
// own USB-serial console and D6/D7 don't exist on this pinout.
//
// Wiring: SenseCAP GPIO22 -> node RX, SenseCAP GPIO23 <- node TX,
// GND <-> GND (crossed TX/RX, shared ground — same convention as the
// XIAO board's wiring, see main README).

void meshtastic_bridge_begin(); // call once from setup(), after Serial.begin()
void meshtastic_bridge_loop();  // call every loop() iteration

// Formats and sends an emergency report as a Meshtastic broadcast text
// message (same LAGE: wire format src/xiao/main.cpp parses). Returns
// whether the local node accepted it for sending — NOT a delivery
// confirmation from the mesh; nothing in this project tracks real
// end-to-end acks (yet).
bool meshtastic_send_emergency(const char *category, const char *type, const char *label);
