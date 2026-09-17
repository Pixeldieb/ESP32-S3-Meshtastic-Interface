#pragma once

// Builds the LVGL screens for the pages described in ui/ui.yaml and loads
// main_menu. Call ui_model_build() once after LVGL is initialized, and
// ui_model_tick() every loop() iteration afterwards (drives the status
// bar clock/blink and the hold_confirm gesture poll).
void ui_model_build();
void ui_model_tick();

// Call these once the Meshtastic bridge exists and actually sends/receives
// something, to flash the status bar's TX/RX indicators. Unused for now —
// deliberately not simulated, so the status bar doesn't lie about traffic
// that isn't happening yet.
void ui_model_notify_tx();
void ui_model_notify_rx();

// Real link state: meshtastic_bridge.cpp calls this once its handshake
// with the external node completes, which drives both the status bar's
// ONLINE/OFFLINE dot and whether trigger_emergency can succeed. Before
// that bridge existed this was a manual "testconnect on/off"-only
// escape hatch (see main.cpp) so the success screen stayed testable
// without the UI lying about connectivity by default — that command
// still works, but now it's overriding a real signal, not standing in
// for a nonexistent one.
void ui_model_set_connected(bool connected);
