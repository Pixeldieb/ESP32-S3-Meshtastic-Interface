#include "meshtastic_proto.h"

#include <RadioLib.h>
#include <esp_system.h>
#include <mbedtls/aes.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h>

#include "lage_db.h"
#include "lora_radio.h"
#include "meshtastic/mesh.pb.h"
#include "ui_model.h"

// Every constant below is copied from meshtastic/firmware's own source
// (read directly, not from memory) during the Issue #36 investigation on
// 2026-09-18 -- see src/sensecap/README.md section 5 for the file-by-file
// citations (RadioInterface.cpp/.h, RadioLibInterface.h, SX126xInterface.cpp,
// Channels.cpp/.h, MeshRadio.h, CryptoEngine.cpp/.h, Router.cpp, MeshTypes.h).

namespace {

// RadioInterface.h: PacketHeader. "This structure has to exactly match the
// wire layout when sent over the radio link." to/from/id are 4 bytes each
// on this platform (NodeNum/PacketId are uint32_t), then 4 single bytes --
// 16 bytes total, no padding on a plain sequence of these field widths.
#pragma pack(push, 1)
struct PacketHeaderRaw {
  uint32_t to;
  uint32_t from;
  uint32_t id;
  uint8_t flags;
  uint8_t channel;
  uint8_t next_hop;
  uint8_t relay_node;
};
#pragma pack(pop)
static_assert(sizeof(PacketHeaderRaw) == 16, "must match MESHTASTIC_HEADER_LENGTH");

constexpr uint32_t kNodenumBroadcast = 0xFFFFFFFFu;    // MeshTypes.h NODENUM_BROADCAST
constexpr uint8_t kHopLimitDefault = 3;                // MeshTypes.h HOP_RELIABLE
constexpr uint8_t kFlagsHopLimitMask = 0x07;
constexpr uint8_t kFlagsHopStartShift = 5;
constexpr uint8_t kFlagsHopStartMask = 0xE0;
constexpr size_t kMaxPayload = 233; // meshtastic_Data_payload_t = PB_BYTES_ARRAY_T(233)

// Channels.h: "16 bytes of random PSK for our _public_ default channel that
// all devices power up on (AES128)". Public, well-known by design -- this is
// not a secret, every stock Meshtastic device ships with it.
const uint8_t kDefaultPsk[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                  0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

// The default (unnamed) primary channel's name for hashing purposes is the
// modem preset's display name (Channels::getName() falls back to this when
// the stored name is empty) -- DisplayFormatters::getModemPresetDisplayName
// returns "LongFast" for LONG_FAST.
const char *kChannelName = "LongFast";

// EU_868 region (RadioInterface.cpp regions[] table).
constexpr float kRegionFreqStartMHz = 869.4f;
constexpr float kRegionFreqEndMHz = 869.65f;

// LONG_FAST preset, non-wide-LoRa (MeshRadio.h modemPresetToParams default case).
constexpr float kBandwidthKHz = 250.0f;
constexpr uint8_t kSpreadingFactor = 11;
constexpr uint8_t kCodingRate = 5;
constexpr uint8_t kSyncWord = 0x2b;      // RadioLibInterface.h
constexpr uint16_t kPreambleLength = 16; // RadioInterface.h preambleLengthDefault
constexpr int8_t kTxPowerDbm = 20;       // safely under region limit (27) and chip max (+22)

uint8_t xorHash(const uint8_t *p, size_t len) {
  uint8_t code = 0;
  for (size_t i = 0; i < len; i++) code ^= p[i];
  return code;
}

// RadioInterface.cpp applyModemConfig(): numChannels = floor((freqEnd -
// freqStart) / (spacing + bw/1000)). For EU_868 + LONG_FAST that's
// floor(0.25 / 0.25) = 1 -- exactly one possible frequency slot, so
// channel_num = hash(name) % 1 = 0 always, no hopping, no ambiguity.
// freq = freqStart + bw/2000 + channel_num*(bw/1000) = 869.4 + 0.125 + 0.
float defaultChannelFrequencyMHz() { return kRegionFreqStartMHz + (kBandwidthKHz / 2000.0f); }

// Channels::generateHash(): xorHash(channel name) ^ xorHash(psk bytes).
uint8_t defaultChannelHash() {
  uint8_t h = xorHash(reinterpret_cast<const uint8_t *>(kChannelName), strlen(kChannelName));
  h ^= xorHash(kDefaultPsk, sizeof(kDefaultPsk));
  return h;
}

uint32_t g_myNodeNum = 0;
bool g_started = false;

// CryptoEngine::encryptAESCtr/initNonce: nonce = 8 bytes packet ID (LE) + 4
// bytes sending node number (LE) + 4 byte block counter starting at 0. CTR
// mode is symmetric (CryptoEngine::decrypt()'s own comment: "For CTR, the
// implementation is the same"), so one function does both directions.
// mbedtls_aes_crypt_ctr increments the whole 16-byte nonce_counter as a
// single big-endian counter, but for any one packet here (well under 240
// bytes = 15 AES blocks) that's indistinguishable from "only the last 4
// bytes are the counter" -- it never carries into byte 11.
void aesCtrCrypt(uint32_t fromNode, uint32_t packetId, uint8_t *data, size_t len) {
  uint8_t nonce[16] = {0};
  uint64_t packetId64 = packetId; // upper 32 bits zero: our packet IDs are 32-bit
  memcpy(nonce, &packetId64, sizeof(uint64_t));
  memcpy(nonce + sizeof(uint64_t), &fromNode, sizeof(uint32_t));

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, kDefaultPsk, 128); // CTR always uses the encrypt key schedule
  size_t ncOff = 0;
  uint8_t streamBlock[16] = {0};
  mbedtls_aes_crypt_ctr(&aes, len, &ncOff, nonce, streamBlock, data, data);
  mbedtls_aes_free(&aes);
}

// Not Meshtastic's own node-number algorithm (never found the exact formula
// needed -- doesn't matter: the wire format doesn't care how a node number
// was chosen, only that it's stable and non-zero/non-broadcast).
uint32_t deriveNodeNum() {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t num = (uint32_t)(mac & 0xFFFFFFFFu);
  if (num == 0 || num == kNodenumBroadcast) num = 0x2A2A2A2A;
  return num;
}

// Same LAGE: wire format src/xiao/main.cpp parses (see main README
// "Lagemeldungen (kayna-funkt)"). State short-codes (LGE/NOR/WTG/SAB/SAUS)
// aren't handled here -- this board's UI doesn't have that state concept.
void handleTextMessage(uint32_t from, const char *text) {
  ui_model_notify_rx();
  Serial.printf("[MESH] Text von !%08x: \"%s\"\n", (unsigned)from, text);

  String msg(text);
  if (!msg.startsWith("LAGE:")) return;

  String payload = msg.substring(5);
  int p1 = payload.indexOf(';');
  int p2 = payload.indexOf(';', p1 + 1);
  int p3 = payload.indexOf(';', p2 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0) {
    Serial.println("[MESH] Ungueltiges LAGE-Format");
    return;
  }

  String idPart = payload.substring(0, p1); idPart.trim();
  String kategorie = payload.substring(p1 + 1, p2); kategorie.trim();
  String status = payload.substring(p2 + 1, p3); status.trim();
  String content = payload.substring(p3 + 1); content.trim();

  char fromBuf[12];
  snprintf(fromBuf, sizeof(fromBuf), "!%08x", (unsigned)from);
  String fromNodeStr(fromBuf);

  if (idPart.equalsIgnoreCase("NEU")) {
    int id = lageDbCreate(kategorie, status, content, fromNodeStr);
    Serial.printf("[MESH] Neue Lagemeldung angelegt, ID %d\n", id);
  } else {
    bool ok = lageDbUpdate(idPart.toInt(), kategorie, status, content, fromNodeStr);
    Serial.printf("[MESH] Lagemeldung %d %s\n", idPart.toInt(), ok ? "aktualisiert" : "nicht gefunden");
  }
}

void handleReceivedPacket(const uint8_t *buf, size_t len) {
  if (len < sizeof(PacketHeaderRaw)) return;
  PacketHeaderRaw header;
  memcpy(&header, buf, sizeof(header));

  uint8_t expectedHash = defaultChannelHash();
  if (header.channel != expectedHash) {
    Serial.printf("[MESH] Paket auf fremdem Kanal ignoriert (hash 0x%02x, erwartet 0x%02x fuer \"%s\")\n", header.channel,
                  expectedHash, kChannelName);
    return;
  }

  size_t cipherLen = len - sizeof(PacketHeaderRaw);
  if (cipherLen == 0 || cipherLen > kMaxPayload) return;

  static uint8_t plain[kMaxPayload];
  memcpy(plain, buf + sizeof(PacketHeaderRaw), cipherLen);
  aesCtrCrypt(header.from, header.id, plain, cipherLen);

  meshtastic_Data data = meshtastic_Data_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(plain, cipherLen);
  if (!pb_decode(&stream, meshtastic_Data_fields, &data)) {
    Serial.println("[MESH] Payload-Decode fehlgeschlagen (falscher Kanal-Key oder korrupt)");
    return;
  }

  float rssi = lora_radio_instance().getRSSI();
  float snr = lora_radio_instance().getSNR();
  Serial.printf("[MESH] Paket von !%08x an !%08x, portnum=%d, %u Bytes, RSSI=%.1fdBm SNR=%.1fdB\n", (unsigned)header.from,
                (unsigned)header.to, (int)data.portnum, (unsigned)data.payload.size, rssi, snr);

  if (data.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
    char text[kMaxPayload + 1];
    size_t n = data.payload.size < sizeof(text) - 1 ? data.payload.size : sizeof(text) - 1;
    memcpy(text, data.payload.bytes, n);
    text[n] = '\0';
    handleTextMessage(header.from, text);
  }
}

} // namespace

bool meshtastic_proto_begin() {
  if (!lora_radio_ready()) {
    Serial.println("[MESH] meshtastic_proto_begin: Radio nicht bereit");
    return false;
  }
  g_myNodeNum = deriveNodeNum();

  SX1262 &radio = lora_radio_instance();
  float freq = defaultChannelFrequencyMHz();
  int16_t state = radio.setFrequency(freq);
  if (state == RADIOLIB_ERR_NONE) state = radio.setBandwidth(kBandwidthKHz);
  if (state == RADIOLIB_ERR_NONE) state = radio.setSpreadingFactor(kSpreadingFactor);
  if (state == RADIOLIB_ERR_NONE) state = radio.setCodingRate(kCodingRate);
  if (state == RADIOLIB_ERR_NONE) state = radio.setSyncWord(kSyncWord);
  if (state == RADIOLIB_ERR_NONE) state = radio.setPreambleLength(kPreambleLength);
  if (state == RADIOLIB_ERR_NONE) state = radio.setCRC(true);
  if (state == RADIOLIB_ERR_NONE) state = radio.setOutputPower(kTxPowerDbm);
  // Real firmware makes this a user setting (config.lora.sx126x_rx_boosted_gain,
  // SX126xInterface.cpp); doesn't affect wire-format compatibility, only local
  // receive sensitivity -- enabled here to give a real over-the-air test
  // against another device the best chance of actually being heard.
  if (state == RADIOLIB_ERR_NONE) state = radio.setRxBoostedGainMode(true);

  Serial.printf("[MESH] Meshtastic-Protokoll: Node !%08x, %.3f MHz, BW%.0f SF%d CR4/%d, Sync 0x%02x, Kanal-Hash 0x%02x (\"%s\")\n",
                (unsigned)g_myNodeNum, freq, kBandwidthKHz, kSpreadingFactor, kCodingRate, kSyncWord, defaultChannelHash(),
                kChannelName);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[MESH] Radio-Konfiguration fehlgeschlagen: %d\n", state);
    return false;
  }

  state = radio.startReceive();
  g_started = (state == RADIOLIB_ERR_NONE);
  Serial.printf("[MESH] startReceive() -> %d (%s)\n", state, g_started ? "OK, lausche auf dem Default-Kanal" : "FEHLER");
  return g_started;
}

void meshtastic_proto_loop() {
  if (!g_started) return;
  SX1262 &radio = lora_radio_instance();
  uint16_t irq = radio.getIrqStatus();
  if (!(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) return;

  // readData() (and startReceive(), called again below) clear IRQ status
  // internally -- clearIrqStatus() itself is protected in RadioLib's public
  // API, not meant to be called directly here.
  if (irq & RADIOLIB_SX126X_IRQ_CRC_ERR) {
    Serial.println("[MESH] Paket mit CRC-Fehler verworfen");
  } else {
    size_t len = radio.getPacketLength();
    if (len >= sizeof(PacketHeaderRaw) && len <= 255) {
      static uint8_t buf[255];
      int16_t state = radio.readData(buf, len);
      if (state == RADIOLIB_ERR_NONE) {
        handleReceivedPacket(buf, len);
      } else {
        Serial.printf("[MESH] readData() -> %d\n", state);
      }
    }
  }
  radio.startReceive(); // resume listening
}

bool meshtastic_proto_send_text(const char *text) {
  if (!g_started) {
    Serial.println("[MESH] send_text: Protokoll nicht gestartet");
    return false;
  }

  meshtastic_Data data = meshtastic_Data_init_zero;
  data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
  size_t textLen = strlen(text);
  if (textLen > sizeof(data.payload.bytes)) textLen = sizeof(data.payload.bytes);
  memcpy(data.payload.bytes, text, textLen);
  data.payload.size = textLen;

  uint8_t plain[kMaxPayload];
  pb_ostream_t ostream = pb_ostream_from_buffer(plain, sizeof(plain));
  if (!pb_encode(&ostream, meshtastic_Data_fields, &data)) {
    Serial.println("[MESH] send_text: pb_encode fehlgeschlagen");
    return false;
  }
  size_t plainLen = ostream.bytes_written;

  uint32_t packetId = esp_random();
  if (packetId == 0) packetId = 1;

  aesCtrCrypt(g_myNodeNum, packetId, plain, plainLen);

  PacketHeaderRaw header;
  header.to = kNodenumBroadcast;
  header.from = g_myNodeNum;
  header.id = packetId;
  header.flags = kHopLimitDefault & kFlagsHopLimitMask;
  header.flags |= (kHopLimitDefault << kFlagsHopStartShift) & kFlagsHopStartMask;
  header.channel = defaultChannelHash();
  header.next_hop = 0;
  header.relay_node = 0;

  uint8_t wire[sizeof(PacketHeaderRaw) + kMaxPayload];
  memcpy(wire, &header, sizeof(header));
  memcpy(wire + sizeof(header), plain, plainLen);
  size_t wireLen = sizeof(header) + plainLen;

  SX1262 &radio = lora_radio_instance();
  int16_t state = radio.transmit(wire, wireLen);
  Serial.printf("[MESH] Sende \"%s\" (id=0x%08x, %u Bytes) -> transmit() = %d (%s)\n", text, (unsigned)packetId,
                (unsigned)wireLen, state, state == RADIOLIB_ERR_NONE ? "OK" : "FEHLER");
  radio.startReceive(); // resume listening (transmit() leaves the radio in standby)
  return state == RADIOLIB_ERR_NONE;
}

bool meshtastic_send_emergency(const char *category, const char *type, const char *label) {
  char buf[160];
  snprintf(buf, sizeof(buf), "LAGE:NEU;%s;offen;%s (Touch-Terminal, Typ: %s)", category, label, type);
  bool ok = meshtastic_proto_send_text(buf);
  if (ok) ui_model_notify_tx();
  return ok;
}

uint32_t meshtastic_proto_my_node_num() { return g_myNodeNum; }
