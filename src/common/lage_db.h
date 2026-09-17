#pragma once
#include <Arduino.h>

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