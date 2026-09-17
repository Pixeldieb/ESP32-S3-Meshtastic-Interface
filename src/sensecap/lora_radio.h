#pragma once
#include <Arduino.h>

// Onboard SX1262 LoRa radio (Weg 1 — direct, not via an external
// Meshtastic node). EXPERIMENTAL: raw point-to-point LoRa only, no
// Meshtastic protocol (framing, encryption, routing, channels) — this
// does NOT interoperate with real Meshtastic nodes on the same channel.
// A validation step to see whether the hardware path (SPI + IO-expander
// -mediated CS/RST/BUSY/DIO1) even works, not a finished feature.
// See the project issue tracker for the real Meshtastic-compatible
// integration this would need to grow into.

// Initializes the SX1262 and returns whether it responded. Call once.
bool lora_radio_begin();

// Sends `text` as a single raw LoRa packet. Returns whether the local
// transmit() call succeeded — no delivery confirmation, no receiver
// compatibility guarantee (see the file comment above).
bool lora_radio_send_test(const char *text);
