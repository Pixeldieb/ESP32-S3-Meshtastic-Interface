#pragma once
#include <Arduino.h>

// Real wall-clock time, settable over Serial (no RTC battery or NTP/WiFi
// on this board yet, so it resets to "unset" on every power cycle).
// Previously the status bar showed millis()-based uptime instead, which
// looked like a real clock but wasn't one — this replaces that.

bool wallClockIsSet();

// Sets the clock. Call once, e.g. from a serial command.
void wallClockSet(int year, int month, int day, int hour, int minute, int second);

// "HH:MM:SS", or "--:--:--" if wallClockIsSet() is false.
String wallClockNowHMS();

// Sets the clock from this firmware's __DATE__/__TIME__ (the build
// machine's local time when it was compiled) — call once at boot.
// "Vorerst" (for now): this is only ever as accurate as build-to-boot
// latency (normally well under a minute for this project's flash
// workflow), and it's still the *build* machine's clock/timezone, not
// necessarily the station's. A real fix (NTP once there's WiFi/a
// Meshtastic time source, or a battery-backed RTC) replaces this later;
// `settime` always overrides whatever this set.
void wallClockSetFromBuildTime();

// Feed one line read from Serial. Recognizes:
//   settime YYYY-MM-DD HH:MM:SS
// Anything else is ignored (so main.cpp can just forward every line here
// without needing its own command parser for this).
void wallClockHandleSerialLine(const String &line);
