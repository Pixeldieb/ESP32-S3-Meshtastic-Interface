#include "wall_clock.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace {
bool g_is_set = false;
}

bool wallClockIsSet() { return g_is_set; }

void wallClockSet(int year, int month, int day, int hour, int minute, int second) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  time_t epoch = mktime(&t);

  struct timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
  g_is_set = true;

  Serial.printf("[CLOCK] gestellt auf %04d-%02d-%02d %02d:%02d:%02d\n",
                year, month, day, hour, minute, second);
}

String wallClockNowHMS() {
  if (!g_is_set) return "--:--:--";
  time_t now;
  time(&now);
  struct tm t;
  localtime_r(&now, &t);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  return String(buf);
}

void wallClockSetFromBuildTime() {
  // __DATE__ = "Mmm dd yyyy" (day is space-padded, e.g. "Sep  7 2026"),
  // __TIME__ = "hh:mm:ss" — both standard, compiler-filled, in whatever
  // local time/timezone the machine running the compiler was set to.
  static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char mon_str[4] = {0};
  int day, year, hour, minute, second;
  sscanf(__DATE__, "%3s %d %d", mon_str, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  int month = 1;
  for (int i = 0; i < 12; i++) {
    if (strncmp(mon_str, months[i], 3) == 0) { month = i + 1; break; }
  }
  wallClockSet(year, month, day, hour, minute, second);
  Serial.println("[CLOCK] (aus Build-Zeitpunkt, vorlaeufig — mit 'settime' korrigierbar)");
}

void wallClockHandleSerialLine(const String &line) {
  if (!line.startsWith("settime ")) return;

  int year, month, day, hour, minute, second;
  int n = sscanf(line.c_str() + 8, "%d-%d-%d %d:%d:%d", &year, &month, &day,
                 &hour, &minute, &second);
  if (n != 6) {
    Serial.println("[CLOCK] Format: settime YYYY-MM-DD HH:MM:SS");
    return;
  }
  wallClockSet(year, month, day, hour, minute, second);
}
