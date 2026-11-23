#pragma once

// Server-role (repeater / room server / sensor) display task. Upstream moved this
// here from examples/simple_repeater and examples/simple_room_server; the observer
// build extends it with the activity dashboard, live display.timeout / flip, the
// WebConfig setup screen and touch toggle. Each variant's target.cpp creates
// `UITask ui_task(&board, &display)`.

#include <helpers/ui/BaseUITask.h>
#include <helpers/CommonCLI.h>

#ifdef DISPLAY_ACTIVITY_DASHBOARD
  #ifndef DISPLAY_REDRAW_ON_CHANGE
    #error "DISPLAY_ACTIVITY_DASHBOARD needs DISPLAY_REDRAW_ON_CHANGE: without it every frame clears the whole screen"
  #endif
  #include <helpers/RadioActivityWindow.h>
  #include <helpers/ui/ObserverDashboard.h>

#endif

#ifdef DISPLAY_TOUCH_TOGGLE
  #include <helpers/ui/CHSC6XTouch.h>
#endif

class UITask : public BaseUITask {
  unsigned long _next_read;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  const char* _role_label = "";
  unsigned long _powering_off_at = 0;
  unsigned long _started_at = 0;

#ifdef DISPLAY_REDRAW_ON_CHANGE
  uint32_t _last_frame_signature = 0;
  bool _frame_valid = false;

  uint32_t getFrameSignature();
#endif

#ifdef DISPLAY_ACTIVITY_DASHBOARD
  RadioActivityWindow* _activity = NULL;
  unsigned long _next_activity = 0;
  uint32_t _row_signatures[ObserverDashboard::ROW_COUNT] = {0};
  bool _rows_valid = false;   // true only while the dashboard is the drawn screen

  bool buildDashboardContext(ObserverDashboard::Context* ctx);
  void renderDashboard();
  void updateActivityRows();
#endif

#ifdef DISPLAY_TOUCH_TOGGLE
  CHSC6XTouch _touch;
  unsigned long _next_touch = 0;

  void toggleDisplay(const char* source);
#endif

#ifdef WITH_MQTT_BRIDGE
  MQTTPrefs* _observer_prefs = NULL;
#endif
  unsigned long _timeout_seen = 0;   // to notice a live `display.timeout` change
  uint8_t _flip_seen = 0xFF;         // 0xFF forces the first apply
  void applyDisplayFlip();

  unsigned long displayTimeoutMillis() const;

protected:
  long renderCurrScreen() override;

public:
  UITask(mesh::MainBoard* board, DisplayDriver* display) : BaseUITask(board, display) { _next_read = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, const char* role_lbl);

#ifdef WITH_MQTT_BRIDGE
  // Supplies `display.timeout`, which is read live so a config change applies
  // without a reboot. Call before begin().
  void setObserverPrefs(MQTTPrefs* prefs) { _observer_prefs = prefs; }
#endif

#ifdef DISPLAY_ACTIVITY_DASHBOARD
  void setActivityWindow(RadioActivityWindow* activity) { _activity = activity; }
#endif

  void loop() override;
};
