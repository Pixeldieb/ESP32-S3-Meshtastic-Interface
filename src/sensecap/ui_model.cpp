#include "ui_model.h"

#include <Arduino.h>
#include <lvgl.h>

#include "lage_db.h"
#include "meshtastic_proto.h"
#include "station_config.h"
#include "wall_clock.h"

// Hand-built mirror of ui/ui.yaml's main_menu -> {fire,police,ambulance,
// information}_menu -> emergency_confirmation flow, plus the chrome
// (status bar) and hardware-input model (context bar, hold_confirm) from
// ui/README.md Abschnitt 19. IDs/labels/parameters below must stay in
// sync with ui.yaml by hand for now.

namespace {

constexpr int32_t SCR = 480;
constexpr int32_t CHROME_TOP = 34;  // persistent status bar height
constexpr int32_t TITLE_H = 40;     // per-screen title band (when shown)
constexpr int32_t CTXBAR_H = 70;
constexpr int32_t TILE_W = 210;
constexpr int32_t TILE_H = 150;
constexpr int32_t TILE_GAP = 16;

constexpr uint32_t COLOR_PRIMARY_BLUE = 0x10537E;
constexpr uint32_t COLOR_GREEN = 0x62B22E;
constexpr uint32_t COLOR_TEAL = 0x04A098;
constexpr uint32_t COLOR_WARN_RED = 0xB22E2E;
constexpr uint32_t COLOR_CHROME_BG = 0x0A2E45;
constexpr uint32_t COLOR_DIM = 0x3A5A6E;

const unsigned long HOLD_CONFIRM_DURATION_MS = 3000;
const unsigned long INDICATOR_FLASH_MS = 400;

struct EmergencyChoice {
  const char *category;
  const char *type;
  const char *label;
};

// Plain aggregate on purpose (no default member initializers, no
// constructor): this core is built with an older C++ standard where a
// type with in-class initializers stops being an aggregate and
// `x = {...}` list-assignment no longer works. `CtxSlot ctx[3] = {};`
// still zero-initializes everything, and `ctx[i] = CtxSlot{...};` still
// works as aggregate-init-then-copy-assign.
struct CtxSlot {
  bool active;
  const char *symbol;
  const char *label;
  uint32_t color;
  lv_event_cb_t cb;
  void *user_data;
};

lv_obj_t *g_scr_main = nullptr;
lv_obj_t *g_scr_fire = nullptr;
lv_obj_t *g_scr_police = nullptr;
lv_obj_t *g_scr_ambulance = nullptr;
lv_obj_t *g_scr_info = nullptr;
lv_obj_t *g_scr_confirm = nullptr;
lv_obj_t *g_scr_situation = nullptr;
lv_obj_t *g_scr_crisis = nullptr;
lv_obj_t *g_scr_emergency_details = nullptr;
lv_obj_t *g_emergency_details_label = nullptr;
lv_obj_t *g_scr_transmission = nullptr;
lv_obj_t *g_transmission_label = nullptr;
lv_obj_t *g_scr_transmission_failed = nullptr;
lv_obj_t *g_transmission_failed_label = nullptr;
lv_obj_t *g_scr_history = nullptr;
int g_case_number_counter = 0;

lv_obj_t *g_confirm_label = nullptr;
lv_obj_t *g_confirm_back_target = nullptr;
EmergencyChoice g_selected{};

// --- emergency_transmission state ---
// meshtastic_proto.cpp's meshtastic_send_emergency() call is real and
// synchronous (see start_transmission) and only tells us the radio locally
// accepted the send. Genuine delivery confirmation is a real ACK from the
// dispatch node, which can take a few seconds to come back over the air --
// see ui_model_tick()'s handling of g_transmission_pending, which now waits
// up to EMERGENCY_ACK_TIMEOUT_MS for meshtastic_proto_emergency_ack_received()
// instead of always finishing after a fixed delay. If the local send itself
// already failed (no point waiting for an ACK that was never requested),
// TRANSMISSION_LOCAL_FAIL_DELAY_MS is used instead -- just long enough for
// "senden..." to be visible before showing the failure, like before.
const unsigned long TRANSMISSION_LOCAL_FAIL_DELAY_MS = 2000;
const unsigned long EMERGENCY_ACK_TIMEOUT_MS = 8000;
bool g_transmission_pending = false;
unsigned long g_transmission_started_at = 0;
int g_transmission_db_id = -1;

// Whether we have a real Meshtastic link. Set by main.cpp's setup() once
// lora_radio_begin() + meshtastic_proto_begin() both succeed (real onboard
// radio, initialized and listening on the default channel) — drives the
// status bar's ONLINE/OFFLINE dot. A "testconnect on/off" serial command
// (main.cpp) can override it for manual QA of the success screen without
// the UI silently lying about connectivity by default.
bool g_meshtastic_connected = false;

// --- hold_confirm state (single long-press button, 3s — see
// confirm_btn_press_cb for why this isn't the two-slot-held-together
// gesture ui.yaml originally described) ---
lv_obj_t *g_hold_bar = nullptr;
unsigned long g_hold_started_at = 0;
bool g_hold_in_progress = false;

// --- status bar ---
// Tried this as a single lv_layer_top() overlay shared by every screen —
// broke rendering badly (only the status bar's own pixels ever showed,
// rest of the screen went blank/stale). LVGL's partial-refresh dirty-
// rect tracking doesn't reliably composite a changing top-layer object
// over independently-changing screen content underneath it. Simpler and
// provenly-correct: build the same status bar as an ordinary child of
// *every* screen (see make_screen()) and update all copies in lockstep.
struct StatusBarWidgets {
  lv_obj_t *time_label;
  lv_obj_t *tx_dot;
  lv_obj_t *rx_dot;
  lv_obj_t *power_dot;
  lv_obj_t *power_lbl;
};
constexpr int MAX_SCREENS = 16; // 12 screens built today, headroom for more
lv_obj_t *g_status_bar_screens[MAX_SCREENS]; // parallel to g_status_bars[]
StatusBarWidgets g_status_bars[MAX_SCREENS];
int g_status_bar_count = 0;
unsigned long g_last_tx_flash = 0;
unsigned long g_last_rx_flash = 0;
unsigned long g_last_clock_update = 0;

void build_status_bar_on(lv_obj_t *scr); // defined below; used by make_screen()

// Tried lv_scr_load_anim(FADE_ON) here to smooth out the transition
// glitch — made it much worse (many more partial redraws means many
// more chances for our un-synchronized direct-framebuffer flush to tear
// mid-scanout). Back to a plain, single instant load until the flush
// path has real double buffering.
void nav_to(lv_obj_t *target) {
  if (!target) return;
  lv_scr_load(target);
}

// Buttons kept their "pressed" highlight after navigating away because
// the screen swap happens before LVGL's own release/state-clear step
// reaches the tapped object (the object persists — screens are never
// deleted — so the stale state was still visible next time it was shown).
void clear_pressed(lv_obj_t *obj) {
  if (obj) lv_obj_clear_state(obj, LV_STATE_PRESSED | LV_STATE_FOCUSED);
}

lv_obj_t *make_screen(const char *title) {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_PRIMARY_BLUE), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  build_status_bar_on(scr);

  if (title) {
    lv_obj_t *title_lbl = lv_label_create(scr);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, CHROME_TOP + 8);
  }
  return scr;
}

int32_t content_top(bool has_title) {
  return has_title ? CHROME_TOP + TITLE_H : CHROME_TOP + 10;
}

void style_tile(lv_obj_t *btn) {
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_TEAL), 0);
  lv_obj_set_style_border_width(btn, 2, 0);
}

// slot: 0=top_left(sel_1) 1=top_right(sel_2) 2=bottom_left(sel_3) 3=bottom_right(sel_4)
void position_tile(lv_obj_t *btn, int slot, int32_t grid_top) {
  int col = slot % 2;
  int row = slot / 2;
  int32_t x = 20 + col * (TILE_W + TILE_GAP);
  int32_t y = grid_top + row * (TILE_H + TILE_GAP);
  lv_obj_set_size(btn, TILE_W, TILE_H);
  lv_obj_set_pos(btn, x, y);
}

// Icons are LVGL's built-in symbol glyphs, not real pictograms — there's
// no built-in fire-truck/police-badge/ambulance-cross glyph set. Good
// enough to give kids a consistent shape+color to recognize per screen;
// real custom icon assets are a follow-up (see chat/README notes).
lv_obj_t *add_tile(lv_obj_t *scr, int slot, int32_t grid_top, const char *symbol, const char *label) {
  lv_obj_t *btn = lv_btn_create(scr);
  style_tile(btn);
  position_tile(btn, slot, grid_top);

  lv_obj_t *col = lv_obj_create(btn);
  lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 4, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon = lv_label_create(col);
  lv_label_set_text(icon, symbol);
  lv_obj_set_style_text_color(icon, lv_color_hex(COLOR_PRIMARY_BLUE), 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);

  lv_obj_t *lbl = lv_label_create(col);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_PRIMARY_BLUE), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

  return btn;
}

// Always lays out a full-width 3-slot band, so every screen visually
// reads as "the same 3 context buttons" even where only 1-2 are active
// (see ui/README.md Abschnitt 19: ctx_1/ctx_2/ctx_3 are fixed hardware
// roles). Inactive slots stay empty rather than getting a fake button.
void build_context_bar(lv_obj_t *scr, const CtxSlot slots[3]) {
  lv_obj_t *band = lv_obj_create(scr);
  lv_obj_set_size(band, SCR, CTXBAR_H);
  lv_obj_set_pos(band, 0, SCR - CTXBAR_H);
  lv_obj_set_style_bg_color(band, lv_color_hex(COLOR_CHROME_BG), 0);
  lv_obj_set_style_border_width(band, 0, 0);
  lv_obj_set_style_radius(band, 0, 0);
  lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);

  int32_t w = (SCR - 40) / 3 - 10;
  for (int i = 0; i < 3; i++) {
    if (!slots[i].active) continue;
    int32_t x = 20 + i * (w + 10);

    lv_obj_t *btn = lv_btn_create(band);
    lv_obj_set_size(btn, w, CTXBAR_H - 20);
    lv_obj_set_pos(btn, x, 10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(slots[i].color), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    if (slots[i].cb) lv_obj_add_event_cb(btn, slots[i].cb, LV_EVENT_CLICKED, slots[i].user_data);

    lv_obj_t *row = lv_obj_create(btn);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);

    if (slots[i].symbol) {
      lv_obj_t *icon = lv_label_create(row);
      lv_label_set_text(icon, slots[i].symbol);
    }
    if (slots[i].label) {
      lv_obj_t *lbl = lv_label_create(row);
      lv_label_set_text(lbl, slots[i].label);
    }
  }
}

// ---------------------------------------------------------------------
// Navigation / action callbacks
// ---------------------------------------------------------------------

void nav_cb(lv_event_t *e) {
  clear_pressed(lv_event_get_target(e));
  lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(e);
  nav_to(target);
}

void emergency_select_cb(lv_event_t *e) {
  clear_pressed(lv_event_get_target(e));
  auto *choice = (EmergencyChoice *)lv_event_get_user_data(e);
  g_selected = *choice;
  g_confirm_back_target = lv_scr_act();
  lv_label_set_text_fmt(
      g_confirm_label,
      "%s melden\n\n"
      "ACHTUNG: Diese Meldung geht sofort an die Leitstelle.\n"
      "Nur bei echtem Notfall ausloesen.\n"
      "Missbrauch (Falschalarm) wird strafrechtlich verfolgt\n"
      "und kann echte Hilfe verzoegern.\n\n"
      "Zum Bestaetigen die Bestaetigen-Taste\n"
      "3 Sekunden gedrueckt halten.",
      choice->label);
  g_hold_in_progress = false;
  if (g_hold_bar) lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
  nav_to(g_scr_confirm);
}

void confirm_cancel_cb(lv_event_t *e) {
  clear_pressed(lv_event_get_target(e));
  g_hold_in_progress = false;
  if (g_hold_bar) lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
  if (g_confirm_back_target) nav_to(g_confirm_back_target);
}

// One-line timestamp for display: real wall-clock if it's been set
// (see wall_clock.h), otherwise honestly labelled uptime.
String format_timestamp() {
  if (wallClockIsSet()) return wallClockNowHMS();
  unsigned long s = millis() / 1000;
  char buf[40];
  snprintf(buf, sizeof(buf), "Laufzeit %02lu:%02lu:%02lu (Uhrzeit nicht gestellt)",
           (s / 3600) % 24, (s / 60) % 60, s % 60);
  return String(buf);
}

// Set by the real meshtastic_send_emergency() call in start_transmission:
// whether the radio locally accepted the send, NOT whether the dispatch
// node actually received it. See ui_model_tick()'s handling of
// g_transmission_pending for how the real ACK wait works.
bool g_last_send_ok = false;

void start_transmission(const char *sending_verb) {
  lv_label_set_text_fmt(g_transmission_label, "%s\n\n" LV_SYMBOL_LOOP " %s ...",
                        g_selected.label, sending_verb);
  g_last_send_ok = meshtastic_send_emergency(g_selected.category, g_selected.type, g_selected.label);
  g_transmission_pending = true;
  g_transmission_started_at = millis();
  nav_to(g_scr_transmission);
}

void do_trigger_emergency() {
  Serial.printf("[UI] trigger_emergency: category=%s type=%s label=%s\n",
                g_selected.category, g_selected.type, g_selected.label);
  g_hold_in_progress = false;
  if (g_hold_bar) lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);

  g_case_number_counter++;
  g_transmission_db_id = lageDbCreate(g_selected.category, "wird uebermittelt",
                                      g_selected.label, "!lokal-touch");
  start_transmission("Meldung wird gesendet");
}

void retry_transmission_cb(lv_event_t *e) {
  clear_pressed(lv_event_get_target(e));
  if (g_transmission_db_id >= 0) {
    lageDbUpdate(g_transmission_db_id, g_selected.category, "wird uebermittelt",
                 g_selected.label, "!lokal-touch");
  }
  start_transmission("Meldung wird erneut gesendet");
}

void transmission_failed_cancel_cb(lv_event_t *e) {
  clear_pressed(lv_event_get_target(e));
  nav_to(g_scr_main);
}

void finish_transmission(bool success) {
  if (success) {
    lageDbUpdate(g_transmission_db_id, g_selected.category, "uebermittelt",
                 g_selected.label, "!lokal-touch");
    lv_label_set_text_fmt(
        g_emergency_details_label,
        "%s\n\n"
        "Vorgangsnummer: VG-%04d\n"
        "Zeitstempel: %s\n"
        "Notfallsaeule: %s\n"
        "Ort: unbekannt (kein GPS)\n\n"
        "Bitte notieren.",
        g_selected.label, g_case_number_counter, format_timestamp().c_str(),
        station_config().stationId.c_str());
    nav_to(g_scr_emergency_details);
  } else {
    lageDbUpdate(g_transmission_db_id, g_selected.category, "fehlgeschlagen",
                 g_selected.label, "!lokal-touch");
    lv_label_set_text_fmt(
        g_transmission_failed_label,
        "%s\n\n"
        LV_SYMBOL_WARNING " Uebertragung fehlgeschlagen.\n\n"
        "Bitte erneut versuchen. Wenn das Problem\n"
        "bestehen bleibt, lokalen Kontakt kontaktieren:\n\n"
        "%s (%s)",
        g_selected.label, station_config().operatorName.c_str(),
        station_config().localContact.c_str());
    nav_to(g_scr_transmission_failed);
  }
}

// hold_confirm, single-touch version: this panel's touch controller only
// ever reports one point at a time (confirmed on real hardware — no
// multitouch), so the original ui.yaml idea of holding two context slots
// at once isn't achievable here. Falls back to a single long-press
// button instead, using LVGL's normal PRESSED/PRESSING/RELEASED events
// on the one indev — no raw touch polling needed for this anymore.
void confirm_btn_press_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED) {
    g_hold_in_progress = true;
    g_hold_started_at = millis();
    if (g_hold_bar) lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
  } else if (code == LV_EVENT_PRESSING && g_hold_in_progress) {
    unsigned long held = millis() - g_hold_started_at;
    int32_t pct = (int32_t)((held * 100) / HOLD_CONFIRM_DURATION_MS);
    if (pct > 100) pct = 100;
    if (g_hold_bar) lv_bar_set_value(g_hold_bar, pct, LV_ANIM_OFF);
    if (held >= HOLD_CONFIRM_DURATION_MS) {
      g_hold_in_progress = false;
      do_trigger_emergency();
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    g_hold_in_progress = false;
    if (g_hold_bar) lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
  }
}

// ---------------------------------------------------------------------
// Screen builders
// ---------------------------------------------------------------------

lv_obj_t *build_department_menu(const char *title, const char *symbol,
                                 const EmergencyChoice items[4], lv_obj_t *back_target) {
  static EmergencyChoice storage[3][4]; // fire, police, ambulance — 3 calls total
  static int menu_index = 0;
  int row = menu_index++;

  lv_obj_t *scr = make_screen(title);
  int32_t top = content_top(true);
  for (int i = 0; i < 4; i++) {
    storage[row][i] = items[i];
    // Sub-items share the department's icon (see add_tile comment above):
    // per-item pictograms (Brand vs. Chemie vs. ...) don't have a good
    // built-in glyph, and a wrong-looking icon would confuse more than
    // plain text would.
    lv_obj_t *tile = add_tile(scr, i, top, symbol, items[i].label);
    lv_obj_add_event_cb(tile, emergency_select_cb, LV_EVENT_CLICKED, &storage[row][i]);
  }

  CtxSlot ctx[3] = {};
  ctx[1] = CtxSlot{true, LV_SYMBOL_LEFT, "Zurueck", COLOR_GREEN, nav_cb, back_target};
  build_context_bar(scr, ctx);
  return scr;
}

lv_obj_t *build_info_list_page(const char *title, const char *categoryFilter, lv_obj_t *back_target) {
  lv_obj_t *scr = make_screen(title);
  int32_t top = content_top(true);

  lv_obj_t *list = lv_obj_create(scr);
  lv_obj_set_size(list, SCR - 40, SCR - CTXBAR_H - top - 10);
  lv_obj_set_pos(list, 20, top);
  lv_obj_set_style_bg_color(list, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(list, 10, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 10, 0);
  lv_obj_set_style_pad_row(list, 8, 0);
  // No momentum/scroll-animation on drag — Hoch/Runter buttons drive it
  // instantly instead (see the context bar below); one less animated
  // thing fighting the un-synchronized flush.
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

  LageMeldungSummary rows[10];
  int count = lageDbGetRecentSummaries(rows, 10);
  if (count == 0) {
    lv_obj_t *empty = lv_label_create(list);
    lv_label_set_text(empty, "Keine Lagemeldungen vorhanden.");
  }
  for (int i = 0; i < count; i++) {
    if (categoryFilter && rows[i].kategorie != categoryFilter) continue;
    lv_obj_t *card = lv_obj_create(list);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F6), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_label_create(card);
    lv_label_set_text_fmt(header, "#%d  %s / %s", rows[i].id, rows[i].kategorie.c_str(), rows[i].status.c_str());
    lv_obj_set_style_text_color(header, lv_color_hex(COLOR_PRIMARY_BLUE), 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);

    lv_obj_t *body = lv_label_create(card);
    lv_label_set_text(body, rows[i].text.c_str());
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
  }

  CtxSlot ctx[3] = {};
  ctx[0] = CtxSlot{true, LV_SYMBOL_UP, "Hoch", COLOR_TEAL, [](lv_event_t *e) {
               lv_obj_t *l = (lv_obj_t *)lv_event_get_user_data(e);
               lv_obj_scroll_by(l, 0, 80, LV_ANIM_OFF);
             }, list};
  ctx[1] = CtxSlot{true, LV_SYMBOL_LEFT, "Zurueck", COLOR_GREEN, nav_cb, back_target};
  ctx[2] = CtxSlot{true, LV_SYMBOL_DOWN, "Runter", COLOR_TEAL, [](lv_event_t *e) {
               lv_obj_t *l = (lv_obj_t *)lv_event_get_user_data(e);
               lv_obj_scroll_by(l, 0, -80, LV_ANIM_OFF);
             }, list};
  build_context_bar(scr, ctx);
  return scr;
}

struct HistoryCategory {
  const char *key;
  const char *label;
  const char *icon;
};
const HistoryCategory kHistoryCategories[] = {
    {"fire_department", "Feuerwehr", LV_SYMBOL_WARNING},
    {"police", "Polizei", LV_SYMBOL_EYE_OPEN},
    {"ambulance", "Krankenwagen", LV_SYMBOL_PLUS},
};

// Notmeldungshistorie: every lage_db entry (both locally triggered ones
// and anything a future Meshtastic bridge writes in) grouped under a
// folder header per "Ereignis" (the department/category it belongs to).
lv_obj_t *build_history_page(lv_obj_t *back_target) {
  lv_obj_t *scr = make_screen("Notmeldungshistorie");
  int32_t top = content_top(true);

  lv_obj_t *list = lv_obj_create(scr);
  lv_obj_set_size(list, SCR - 40, SCR - CTXBAR_H - top - 10);
  lv_obj_set_pos(list, 20, top);
  lv_obj_set_style_bg_color(list, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(list, 10, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 10, 0);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

  LageMeldungSummary rows[20];
  int count = lageDbGetRecentSummaries(rows, 20);
  bool any = false;

  for (const HistoryCategory &cat : kHistoryCategories) {
    bool has_entry = false;
    for (int i = 0; i < count; i++) {
      if (rows[i].kategorie == cat.key) { has_entry = true; break; }
    }
    if (!has_entry) continue;
    any = true;

    lv_obj_t *folder = lv_label_create(list);
    lv_label_set_text_fmt(folder, "%s  %s", cat.icon, cat.label);
    lv_obj_set_style_text_color(folder, lv_color_hex(COLOR_PRIMARY_BLUE), 0);
    lv_obj_set_style_text_font(folder, &lv_font_montserrat_18, 0);

    for (int i = 0; i < count; i++) {
      if (rows[i].kategorie != cat.key) continue;
      lv_obj_t *card = lv_obj_create(list);
      lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F6), 0);
      lv_obj_set_style_radius(card, 8, 0);
      lv_obj_set_style_pad_all(card, 8, 0);
      lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t *header = lv_label_create(card);
      lv_label_set_text_fmt(header, "#%d  %s", rows[i].id, rows[i].status.c_str());
      lv_obj_set_style_text_color(header, lv_color_hex(COLOR_PRIMARY_BLUE), 0);
      lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);

      lv_obj_t *body = lv_label_create(card);
      lv_label_set_text(body, rows[i].text.c_str());
      lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
      lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(body, LV_PCT(100));
    }
  }
  if (!any) {
    lv_obj_t *empty = lv_label_create(list);
    lv_label_set_text(empty, "Keine Notmeldungen vorhanden.");
  }

  CtxSlot ctx[3] = {};
  ctx[0] = CtxSlot{true, LV_SYMBOL_UP, "Hoch", COLOR_TEAL, [](lv_event_t *e) {
               lv_obj_t *l = (lv_obj_t *)lv_event_get_user_data(e);
               lv_obj_scroll_by(l, 0, 80, LV_ANIM_OFF);
             }, list};
  ctx[1] = CtxSlot{true, LV_SYMBOL_LEFT, "Zurueck", COLOR_GREEN, nav_cb, back_target};
  ctx[2] = CtxSlot{true, LV_SYMBOL_DOWN, "Runter", COLOR_TEAL, [](lv_event_t *e) {
               lv_obj_t *l = (lv_obj_t *)lv_event_get_user_data(e);
               lv_obj_scroll_by(l, 0, -80, LV_ANIM_OFF);
             }, list};
  build_context_bar(scr, ctx);
  return scr;
}

// ---------------------------------------------------------------------
// Status bar — built as a normal child of each screen (see make_screen).
// ---------------------------------------------------------------------

void build_status_bar_on(lv_obj_t *scr) {
  lv_obj_t *bar = lv_obj_create(scr);
  lv_obj_set_size(bar, SCR, CHROME_TOP);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_CHROME_BG), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *station = lv_label_create(bar);
  lv_label_set_text(station, station_config().stationId.c_str());
  lv_obj_set_style_text_color(station, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(station, &lv_font_montserrat_14, 0);
  lv_obj_align(station, LV_ALIGN_LEFT_MID, 10, 0);

  // Driven by g_meshtastic_connected (updated in ui_model_tick) — always
  // false/red/OFFLINE until a real Meshtastic bridge exists to report a
  // real link state.
  lv_obj_t *power_dot = lv_obj_create(bar);
  lv_obj_set_size(power_dot, 10, 10);
  lv_obj_set_style_radius(power_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(power_dot, lv_color_hex(COLOR_WARN_RED), 0);
  lv_obj_set_style_border_width(power_dot, 0, 0);
  lv_obj_align(power_dot, LV_ALIGN_LEFT_MID, 120, 0);
  lv_obj_t *power_lbl = lv_label_create(bar);
  lv_label_set_text(power_lbl, "OFFLINE");
  lv_obj_set_style_text_color(power_lbl, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(power_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(power_lbl, LV_ALIGN_LEFT_MID, 134, 0);

  lv_obj_t *time_label = lv_label_create(bar);
  // Real wall-clock (see wall_clock.h) — no RTC battery or NTP/WiFi on
  // this board yet, so it shows "--:--:--" until set once over Serial
  // (settime YYYY-MM-DD HH:MM:SS) and resets to unset on every reboot.
  // Used to show millis()-based uptime here instead, which looked like
  // a real clock but wasn't one and was "komplett falsch".
  lv_label_set_text(time_label, "--:--:--");
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
  lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *tx_lbl = lv_label_create(bar);
  lv_label_set_text(tx_lbl, "TX");
  lv_obj_set_style_text_color(tx_lbl, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(tx_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(tx_lbl, LV_ALIGN_RIGHT_MID, -76, 0);

  lv_obj_t *tx_dot = lv_obj_create(bar);
  lv_obj_set_size(tx_dot, 10, 10);
  lv_obj_set_style_radius(tx_dot, 2, 0);
  lv_obj_set_style_bg_color(tx_dot, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_border_width(tx_dot, 0, 0);
  lv_obj_align(tx_dot, LV_ALIGN_RIGHT_MID, -56, 0);

  lv_obj_t *rx_lbl = lv_label_create(bar);
  lv_label_set_text(rx_lbl, "RX");
  lv_obj_set_style_text_color(rx_lbl, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(rx_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(rx_lbl, LV_ALIGN_RIGHT_MID, -30, 0);

  lv_obj_t *rx_dot = lv_obj_create(bar);
  lv_obj_set_size(rx_dot, 10, 10);
  lv_obj_set_style_radius(rx_dot, 2, 0);
  lv_obj_set_style_bg_color(rx_dot, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_border_width(rx_dot, 0, 0);
  lv_obj_align(rx_dot, LV_ALIGN_RIGHT_MID, -10, 0);

  if (g_status_bar_count < MAX_SCREENS) {
    g_status_bar_screens[g_status_bar_count] = scr;
    g_status_bars[g_status_bar_count] = StatusBarWidgets{time_label, tx_dot, rx_dot, power_dot, power_lbl};
    g_status_bar_count++;
  }
}

void seed_example_lage_db() {
  // "zeige einfach eine selbst populierte Datenbank an erstmal" — no
  // Meshtastic bridge feeds this yet, so we seed a few example entries
  // once at boot purely to prove the info page reads real DB rows.
  LageMeldungSummary existing[1];
  if (lageDbGetRecentSummaries(existing, 1) > 0) return; // already seeded (SPIFFS persists)

  lageDbCreate("fire_department", "offen", "Kellerbrand Mehrfamilienhaus Hauptstrasse 12", "!seed0001");
  lageDbCreate("police", "in Bearbeitung", "Einbruch gemeldet, Streife unterwegs", "!seed0002");
  lageDbCreate("ambulance", "erledigt", "Person verletzt, Rettungsdienst vor Ort", "!seed0003");
}

} // namespace

void ui_model_build() {
  seed_example_lage_db();

  // emergency_confirmation first: department menus need a valid target for it.
  g_scr_confirm = make_screen("Notfall bestaetigen");
  int32_t confirm_top = content_top(true);

  g_confirm_label = lv_label_create(g_scr_confirm);
  lv_obj_set_style_text_color(g_confirm_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_confirm_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(g_confirm_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_confirm_label, SCR - 40);
  lv_obj_set_pos(g_confirm_label, 20, confirm_top);

  g_hold_bar = lv_bar_create(g_scr_confirm);
  lv_obj_set_size(g_hold_bar, SCR - 40, 18);
  lv_obj_align(g_hold_bar, LV_ALIGN_BOTTOM_MID, 0, -(CTXBAR_H + 14));
  lv_bar_set_range(g_hold_bar, 0, 100);
  lv_bar_set_value(g_hold_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_hold_bar, lv_color_hex(COLOR_DIM), 0);
  lv_obj_set_style_bg_color(g_hold_bar, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);

  // Single long-press "Bestaetigen" button above the context bar (no
  // multitouch on this panel — see confirm_btn_press_cb comment).
  // ctx_2 stays a plain single-press cancel.
  lv_obj_t *confirm_btn = lv_btn_create(g_scr_confirm);
  lv_obj_set_size(confirm_btn, SCR - 40, 60);
  lv_obj_align(confirm_btn, LV_ALIGN_BOTTOM_MID, 0, -(CTXBAR_H + 44));
  lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(COLOR_GREEN), 0);
  lv_obj_set_style_radius(confirm_btn, 10, 0);
  lv_obj_t *confirm_lbl = lv_label_create(confirm_btn);
  lv_label_set_text(confirm_lbl, LV_SYMBOL_OK " Bestaetigen (3s halten)");
  lv_obj_center(confirm_lbl);
  lv_obj_add_event_cb(confirm_btn, confirm_btn_press_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(confirm_btn, confirm_btn_press_cb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(confirm_btn, confirm_btn_press_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(confirm_btn, confirm_btn_press_cb, LV_EVENT_PRESS_LOST, nullptr);

  CtxSlot ctx[3] = {};
  ctx[1] = CtxSlot{true, LV_SYMBOL_CLOSE, "Abbruch", COLOR_WARN_RED, confirm_cancel_cb, nullptr};
  build_context_bar(g_scr_confirm, ctx);

  const EmergencyChoice fire_items[4] = {
      {"fire_department", "fire", "Brand"},
      {"fire_department", "chemical", "Chemie"},
      {"fire_department", "traffic_accident", "Verkehrsunfall"},
      {"fire_department", "other", "Andere"},
  };
  const EmergencyChoice police_items[4] = {
      {"police", "burglary", "Einbruch"},
      {"police", "theft", "Diebstahl"},
      {"police", "security", "Sicherheit"},
      {"police", "other", "Andere"},
  };
  const EmergencyChoice ambulance_items[4] = {
      {"ambulance", "injury", "Verletzung"},
      {"ambulance", "emergency_doctor", "Notarzt"},
      {"ambulance", "transport", "Transport"},
      {"ambulance", "other", "Andere"},
  };

  // main_menu: no per-screen title (the status bar already shows the
  // station's canonical ID — a generic "Hauptmenue" heading is redundant).
  g_scr_main = make_screen(nullptr);

  g_scr_fire = build_department_menu("Feuerwehr", LV_SYMBOL_WARNING, fire_items, g_scr_main);
  g_scr_police = build_department_menu("Polizei", LV_SYMBOL_EYE_OPEN, police_items, g_scr_main);
  g_scr_ambulance = build_department_menu("Krankenwagen", LV_SYMBOL_PLUS, ambulance_items, g_scr_main);

  // g_scr_info's shell is built first (empty) so situation/crisis/history
  // — all reached FROM the info menu, not from main_menu — can point
  // their "Zurueck" at it. Same nullptr-capture bug as before otherwise:
  // these back-targets are baked in at CtxSlot construction time, so the
  // target has to already be a valid pointer, not just assigned later.
  // (This was the "Zurueck geht auf Home statt zurueck" report — these
  // three previously pointed at g_scr_main directly, skipping a menu
  // level.)
  g_scr_info = make_screen("Info");

  g_scr_situation = build_info_list_page("Lageinformationen", nullptr, g_scr_info);
  g_scr_crisis = build_info_list_page("Systeminfo Krisenstab", nullptr, g_scr_info);
  g_scr_history = build_history_page(g_scr_info);

  {
    int32_t top = content_top(true);
    lv_obj_t *t1 = add_tile(g_scr_info, 0, top, LV_SYMBOL_LIST, "Lageinformationen\nanfordern");
    lv_obj_add_event_cb(t1, nav_cb, LV_EVENT_CLICKED, g_scr_situation);
    lv_obj_t *t2 = add_tile(g_scr_info, 1, top, LV_SYMBOL_LIST, "Systeminfo\nKrisenstab");
    lv_obj_add_event_cb(t2, nav_cb, LV_EVENT_CLICKED, g_scr_crisis);
    lv_obj_t *t3 = add_tile(g_scr_info, 2, top, LV_SYMBOL_BELL, "Notmeldungs-\nhistorie");
    lv_obj_add_event_cb(t3, nav_cb, LV_EVENT_CLICKED, g_scr_history);

    CtxSlot ctx_info[3] = {};
    ctx_info[1] = CtxSlot{true, LV_SYMBOL_LEFT, "Zurueck", COLOR_GREEN, nav_cb, g_scr_main};
    build_context_bar(g_scr_info, ctx_info);
  }

  {
    int32_t top = content_top(false);
    lv_obj_t *t1 = add_tile(g_scr_main, 0, top, LV_SYMBOL_WARNING, "Feuerwehr");
    lv_obj_add_event_cb(t1, nav_cb, LV_EVENT_CLICKED, g_scr_fire);
    lv_obj_t *t2 = add_tile(g_scr_main, 1, top, LV_SYMBOL_EYE_OPEN, "Polizei");
    lv_obj_add_event_cb(t2, nav_cb, LV_EVENT_CLICKED, g_scr_police);
    lv_obj_t *t3 = add_tile(g_scr_main, 2, top, LV_SYMBOL_PLUS, "Krankenwagen");
    lv_obj_add_event_cb(t3, nav_cb, LV_EVENT_CLICKED, g_scr_ambulance);
    lv_obj_t *t4 = add_tile(g_scr_main, 3, top, LV_SYMBOL_LIST, "Info");
    lv_obj_add_event_cb(t4, nav_cb, LV_EVENT_CLICKED, g_scr_info);

    CtxSlot ctx_main[3] = {}; // root page: nothing to go back to, all inactive
    build_context_bar(g_scr_main, ctx_main);
  }

  // emergency_details / emergency_transmission / emergency_transmission_failed
  // (ui.yaml): built here, after g_scr_main exists, not earlier — their
  // "Zurueck"/"Abbruch" buttons target g_scr_main, and CtxSlot.user_data
  // captures that pointer's value at construction time. Building these
  // before g_scr_main was assigned baked in a nullptr, which is why
  // "Zurueck" silently did nothing on the first version of this screen.
  g_scr_emergency_details = make_screen("Notfall uebermittelt");
  {
    int32_t top = content_top(true);
    lv_obj_t *check = lv_label_create(g_scr_emergency_details);
    lv_label_set_text(check, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(check, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_text_font(check, &lv_font_montserrat_32, 0);
    lv_obj_align(check, LV_ALIGN_TOP_MID, 0, top);

    g_emergency_details_label = lv_label_create(g_scr_emergency_details);
    lv_obj_set_style_text_color(g_emergency_details_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_emergency_details_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(g_emergency_details_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_emergency_details_label, SCR - 40);
    lv_obj_set_pos(g_emergency_details_label, 20, top + 50);

    CtxSlot ctx_details[3] = {};
    ctx_details[1] = CtxSlot{true, LV_SYMBOL_LEFT, "Zurueck", COLOR_GREEN, nav_cb, g_scr_main};
    build_context_bar(g_scr_emergency_details, ctx_details);
  }

  // emergency_transmission: shown while "sending" (simulated — see
  // do_trigger_emergency/finish_transmission). No context bar at all:
  // nothing to do here but wait, matching ui.yaml (no buttons listed).
  g_scr_transmission = make_screen("Notfall wird uebermittelt");
  {
    int32_t top = content_top(true);
    g_transmission_label = lv_label_create(g_scr_transmission);
    lv_obj_set_style_text_color(g_transmission_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_transmission_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(g_transmission_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_transmission_label, SCR - 40);
    lv_obj_set_pos(g_transmission_label, 20, top + 60);
  }

  g_scr_transmission_failed = make_screen("Uebertragung fehlgeschlagen");
  {
    int32_t top = content_top(true);
    g_transmission_failed_label = lv_label_create(g_scr_transmission_failed);
    lv_obj_set_style_text_color(g_transmission_failed_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_transmission_failed_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(g_transmission_failed_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(g_transmission_failed_label, SCR - 40);
    lv_obj_set_pos(g_transmission_failed_label, 20, top);

    CtxSlot ctx_failed[3] = {};
    ctx_failed[0] = CtxSlot{true, LV_SYMBOL_REFRESH, "Erneut senden", COLOR_TEAL, retry_transmission_cb, nullptr};
    ctx_failed[1] = CtxSlot{true, LV_SYMBOL_CLOSE, "Abbruch", COLOR_WARN_RED, transmission_failed_cancel_cb, nullptr};
    build_context_bar(g_scr_transmission_failed, ctx_failed);
  }

  lv_scr_load(g_scr_main); // first load: no fade needed
}

void ui_model_tick() {
  unsigned long now = millis();

  // Only touch the currently visible screen's own status bar copy — the
  // other 7 are off-screen and don't need updating every tick.
  lv_obj_t *active = lv_scr_act();
  int active_idx = -1;
  for (int i = 0; i < g_status_bar_count; i++) {
    if (g_status_bar_screens[i] == active) { active_idx = i; break; }
  }

  if (active_idx >= 0) {
    if (now - g_last_clock_update >= 1000) {
      g_last_clock_update = now;
      lv_label_set_text(g_status_bars[active_idx].time_label, wallClockNowHMS().c_str());
    }
    bool tx_on = (now - g_last_tx_flash) < INDICATOR_FLASH_MS;
    bool rx_on = (now - g_last_rx_flash) < INDICATOR_FLASH_MS;
    lv_obj_set_style_bg_color(g_status_bars[active_idx].tx_dot, lv_color_hex(tx_on ? 0xFFA500 : COLOR_DIM), 0);
    lv_obj_set_style_bg_color(g_status_bars[active_idx].rx_dot, lv_color_hex(rx_on ? COLOR_GREEN : COLOR_DIM), 0);
    lv_obj_set_style_bg_color(g_status_bars[active_idx].power_dot,
                              lv_color_hex(g_meshtastic_connected ? COLOR_GREEN : COLOR_WARN_RED), 0);
    lv_label_set_text(g_status_bars[active_idx].power_lbl, g_meshtastic_connected ? "ONLINE" : "OFFLINE");
  }
  // hold_confirm is now driven entirely by LVGL's PRESSED/PRESSING/
  // RELEASED events on the single confirm button (confirm_btn_press_cb)
  // — no per-tick polling needed here anymore.

  // The actual meshtastic_send_emergency() call already happened (in
  // start_transmission, via meshtastic_proto.cpp). What happens next
  // depends on whether the radio even accepted the send locally:
  if (g_transmission_pending) {
    unsigned long elapsed = now - g_transmission_started_at;
    if (!g_last_send_ok) {
      // Local send already failed (no dispatch configured, radio error) --
      // nothing to wait for. Same short delay as before just so
      // "senden..." doesn't flash past instantly.
      if (elapsed >= TRANSMISSION_LOCAL_FAIL_DELAY_MS) {
        g_transmission_pending = false;
        finish_transmission(false);
      }
    } else if (meshtastic_proto_emergency_ack_received()) {
      // A real ROUTING_APP ACK came back from the dispatch node -- genuine
      // delivery confirmation, finish as soon as it arrives.
      g_transmission_pending = false;
      finish_transmission(true);
    } else if (elapsed >= EMERGENCY_ACK_TIMEOUT_MS) {
      // Radio sent it, but nobody confirmed receipt in time. Report as
      // failure -- we genuinely don't know if it arrived, and this screen
      // must never claim success without real confirmation.
      g_transmission_pending = false;
      finish_transmission(false);
    }
  }
}

void ui_model_set_connected(bool connected) { g_meshtastic_connected = connected; }

void ui_model_notify_tx() { g_last_tx_flash = millis(); }
void ui_model_notify_rx() { g_last_rx_flash = millis(); }
