#pragma once
#include <Arduino.h>

bool lageDbBegin();
int  lageDbCreate(const String& kategorie, const String& status, const String& text, const String& fromNode);
bool lageDbUpdate(int id, const String& kategorie, const String& status, const String& text, const String& fromNode);
void lageDbListSummary(const String& filterKategorie = "", const String& filterStatus = "");
void lageDbShowDetail(int id);