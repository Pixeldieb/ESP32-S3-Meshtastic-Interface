#pragma once
#include <Arduino.h>

// Real Meshtastic-protocol-compatible packet layer on top of the onboard
// SX1262 (see lora_radio.h/.cpp for the radio itself). This is Weg 1 grown
// up: not just raw LoRa anymore, but the actual over-the-air packet format
// stock Meshtastic firmware speaks -- 16-byte plaintext header, AES-CTR
// encrypted protobuf payload, same nonce/key/channel-hash scheme -- so a
// factory-default Meshtastic app or node on the same region+preset can
// receive and decode what we send, and we can decode what they send.
//
// Scope (deliberately, for now): the public default primary channel only
// ("LongFast" name, default PSK) -- the channel every stock Meshtastic
// device listens on before any custom channel config. Region is fixed to
// EU_868 per org policy (see CLAUDE org instructions), which for the
// LONG_FAST preset has exactly one possible frequency slot (869.525 MHz --
// verified against meshtastic/firmware's own RadioInterface.cpp formula,
// see meshtastic_proto.cpp's comments). No custom channels, no PKI/DM
// encryption, no routing/rebroadcast -- we originate and receive on the
// primary channel only, like a simple leaf node.
//
// See lib/meshtastic_proto/README.md for where the protobuf definitions
// (vendored, not hand-written) come from.

// Call once, after lora_radio_begin() has already succeeded.
bool meshtastic_proto_begin();

// Call every loop() iteration -- drives the receive path (polls the radio's
// IRQ status over SPI; DIO1 is behind the IO-expander so we can't use a real
// hardware interrupt for it, see lora_radio.cpp).
void meshtastic_proto_loop();

// Broadcasts a TEXT_MESSAGE_APP packet on the default primary channel.
// Returns whether the radio accepted it for transmission (RadioLib's return
// code) -- NOT a delivery/ack confirmation. Nothing here tracks real
// end-to-end acks (same honesty caveat as the rest of this project).
bool meshtastic_proto_send_text(const char *text);

// Formats and sends an emergency report using the same LAGE: wire format
// src/xiao/main.cpp parses (see main README "Lagemeldungen (kayna-funkt)").
bool meshtastic_send_emergency(const char *category, const char *type, const char *label);

// Our own node number (derived from the ESP32's factory MAC, like real
// Meshtastic firmware does) -- logged at startup, useful for identifying
// this device's packets in a real Meshtastic app while testing.
uint32_t meshtastic_proto_my_node_num();
