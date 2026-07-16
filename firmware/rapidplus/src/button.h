#ifndef _BUTTON_H
#define _BUTTON_H

#include "define.h"
#include "ForteSetting.h"
#include "sensor6035.h"
#include "updateOTA.h"

typedef enum
{
    B_RED,
    B_BLUE,
    B_WHITE
} e_statusbutton;

// ================================================================
// Button event types for deferred processing
// ================================================================
typedef enum
{
    BTN_EVENT_NONE = 0,    // No pending event
    BTN_EVENT_SHORT_PRESS, // Short press (50ms < duration < long-press threshold)
    BTN_EVENT_LONG_PRESS   // Long press (duration >= long-press threshold)
} e_buttonEvent;

// ================================================================
// Per-button state tracked by ISR + polling
// ================================================================
typedef struct
{
    volatile bool rawPressed;       // Current physical state from ISR (true = pressed)
    volatile uint32_t lastEdgeTime; // millis() at last ISR edge (for polling reference)

    // Debounced state — maintained by poll() in loop()
    bool debounced;             // Debounced pressed state
    uint32_t debounceTime;      // Time of last debounce transition
    bool longPressFired;        // Prevents re-fire during same hold
    e_buttonEvent pendingEvent; // Event ready for processing
} ButtonState;

// ================================================================
// Long-press thresholds (ms) — match original Ticker durations
// ================================================================
#define LONG_PRESS_RED_MS 3000   // Red long-press → Setting menu
#define LONG_PRESS_BLUE_MS 3000  // Blue long-press → Calibration
#define LONG_PRESS_WHITE_MS 5000 // White long-press → Review (was calibTime=5000)

class buttonManager
{
private:
    // Debounce + long-press polling
    void pollButton(uint8_t index, uint16_t longPressMs);
    void processEvent(e_statusbutton index, e_buttonEvent event);

    // Short-press business logic (equivalent to old buttonProcess switch cases)
    void handleShortPress_Red();
    void handleShortPress_Blue();
    void handleShortPress_White();

    // Long-press business logic (equivalent to old tickerHandler callbacks)
    void handleLongPress_Red();
    void handleLongPress_Blue();
    void handleLongPress_White();

public:
    buttonManager();

    void buttonStart(); // Attach ISR (call once in setup())
    void loop();        // Poll + process (call every iteration of Arduino loop())

    // Inject a short press from another task (e.g. the web dashboard). Posts to
    // the same pendingEvent queue that loop() drains, so the handler runs in the
    // InputTask context - safe w.r.t. _displayCLD/_PIDControl shared state.
    void postShortPress(e_statusbutton b);
};

extern buttonManager _buttonManager;

#endif
