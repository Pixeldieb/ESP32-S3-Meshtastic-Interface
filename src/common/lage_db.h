#pragma once
#include <Arduino.h>

// created_at/updated_at default to millis() (uptime) -- fine for relative
// ordering, but not a real date/time, and this file is shared across
// boards (some have no real clock at all, e.g. src/xiao). A board with
// access to real wall-clock time (see e.g. sensecap/wall_clock.h) can
// install a provider returning real epoch seconds instead, so timestamps
// read back out are actually meaningful. Optional; defaults to millis().
typedef unsigned long (*LageDbTimeFn)();
void lageDbSetTimeProvider(LageDbTimeFn fn);

bool lageDbBegin();
int  lageDbCreate(const String& kategorie, const String& status, const String& text, const String& fromNode);
bool lageDbUpdate(int id, const String& kategorie, const String& status, const String& text, const String& fromNode);
void lageDbListSummary(const String& filterKategorie = "", const String& filterStatus = "");
void lageDbShowDetail(int id);

// Structured read access for UIs (displays, LVGL, ...) that can't just
// print to Serial like the CLI functions above.
struct LageMeldungSummary {
  int id;
  String kategorie;
  String status;
  String text;
  unsigned long updatedAt;
};

// Fills `out` (capacity `maxCount`) with the most recently updated
// Lagemeldungen, newest first. Returns how many were written.
int lageDbGetRecentSummaries(LageMeldungSummary* out, int maxCount);