// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// If any of these checks fail, make sure you setup the Arduino IDE Board to
// Arduino Pro or Pro Mini (5V, 16Mhz) w/ Atmega 328P'.
#ifndef __AVR_ATmega328P__
#error "Unexpected MCU"
#endif
#if F_CPU != 16000000
#error "Unexpected CPU speed"
#endif

#include "analysis.h"
#include "avr_util.h"
#include "buttons.h"
#include "config.h"
#include "leds.h"
#include "ltc2943.h"
#include "passive_timer.h"

namespace formats {
  static const uint8 kTimeVsCurrent = 1;
  static const uint8 kDetailed = 2;
  static const uint8 kDetailedLabeled = 3;
  static const uint8 kDebug = 4;
}

// Represent the parameters of a measurement mode.
struct Mode {
  const uint8 format;
  const uint16 minor_slots_per_major_slot;

  // format is one of format:* values.
  Mode(uint8 format, uint16 minor_slots_per_major_slot)
  : 
   format(format),
   minor_slots_per_major_slot(minor_slots_per_major_slot) {
  }
};

// Mode table. Indexed by config::modeIndex();
static const Mode modes_table[] = {
  // 0 - Simple format, 1Hz
  Mode(formats::kTimeVsCurrent, 10),
  
  // 1 - Simple format, 10Hz
  Mode(formats::kTimeVsCurrent, 1),
  
  // 2 - Labeled detailed format.  1Hz.
  Mode(formats::kDetailedLabeled, 10),
  
  // 3 - Labeled detailed format.  1Hz.
  Mode(formats::kDetailedLabeled, 1),
  
  // 4 - Unlabeled detailed format.  1Hz.
  Mode(formats::kDetailed, 10),
  
  // 5 - Unlabeled detailed format.  1Hz.
  Mode(formats::kDetailed, 1),
  
  // 6-15 - Debug (1Hz, 10Hz, ...)
  Mode(formats::kDebug, 10),
  Mode(formats::kDebug, 1),
  Mode(formats::kDebug, 10),
  Mode(formats::kDebug, 1),
  Mode(formats::kDebug, 10),
  Mode(formats::kDebug, 1),
  Mode(formats::kDebug, 10),
  Mode(formats::kDebug, 1),
  Mode(formats::kDebug, 10),
  Mode(formats::kDebug, 1),
};

static inline boolean isDebugMode() {
  return config::modeIndex() == 9;
}

// 8 bit enum with main states.
namespace states {
  const uint8 INIT = 1;
  const uint8 REPORTING = 3;
  const uint8 ERROR = 4;
}
static uint8 state = 0;

// INIT state declaration.
class StateInit {
  public:
    static inline void enter();
    static inline void loop();  
};

// REPORTING state declaration.
class StateReporting {
  public:
    static inline void enter();
    static inline void loop();  
  private:
    // Index of modes table entry of current mode.
    static uint8 selected_mode_index;
    
    // Before starting the major and minor slots we first perform the 
    // initial reading. This is also marked as timestamp 0.
    static bool has_last_reading;
       
    // A minor slot is the basic measurement slot. It is used mainly
    // to count device wake ups. 
    static uint32 last_minor_slot_time_millis;
    static uint16 last_minor_slot_charge_ticks_reading;
    
    // Used to track the various slots data.
    static analysis::SlotTracker slot_tracker;

        // For restart button press detection
    static bool last_action_button_state;
};

uint8 StateReporting::selected_mode_index;
bool StateReporting::has_last_reading;
uint32 StateReporting::last_minor_slot_time_millis;
uint16 StateReporting::last_minor_slot_charge_ticks_reading;  
analysis::SlotTracker StateReporting::slot_tracker;
bool StateReporting::last_action_button_state;

// ERROR state declaration.
class StateError {
  public:
    static inline void enter();
    static inline void loop(); 
  private:
    static PassiveTimer time_in_state; 
};
PassiveTimer StateError::time_in_state;

// Arduino setup function. Called once during initialization.
void setup() {;
  Serial.begin(115200);
  
  if (isDebugMode()) {
    printf(F("\nStarted\n"));
  } else {
    printf(F("\n"));
  }

  config::setup();
  
  buttons::setup();
  
  // Let the config value stablizes through the debouncer.
  while (!config::hasStableValue()) {
    config::loop();
  }
  
  // TODO: disabled unused Arduino time interrupts.


  // Initialize the LTC2943 driver and I2C library.
  // TODO: move this to state machine, check error code.
  ltc2943::setup();
  
  StateInit::enter();
 
  // Enable global interrupts.
  sei(); 
  
  // Have an early 'waiting' led bling to indicate normal operation.
  leds::activity.action(); 
}

//static inline void 

void StateInit::enter() {
  state = states::INIT;
  if (isDebugMode()) {
    printf(F("# State: INIT\n"));
  }
}

void StateInit::loop() {
  if (ltc2943::init()) {
    StateReporting::enter();
  } else {
    printf(F("# LTC2943 init failed (is power connected?)\n"));
    StateError::enter();
  }
}

void StateReporting::enter() {
  state = states::REPORTING;
  has_last_reading = false;
  last_action_button_state = buttons:: isActionButtonPressed();
  // We switch modes only when entering the reporting state. If changed
  // while in the reporting state, we reenter it. This provides a graceful
  // transition.
  selected_mode_index = config::modeIndex();
  if (isDebugMode()) {
    printf(F("# Mode: %d\n"), selected_mode_index);
    printf(F("# State: REPORTING.0\n"));
  }
}

void StateReporting::loop() {
  // Check for mode change.
  if (config::modeIndex() != selected_mode_index) {
    if (isDebugMode()) {
      printf(F("# Mode changed\n"));
    }
    printf(F("\n"));
    // The switch is done when reentering the state. This provides graceful 
    // transition.
    StateReporting::enter();
    return;
  }
  
  // If button pressed (low to high transition) then reenter state.
  {
    const bool new_action_button_state = buttons:: isActionButtonPressed();
    const bool button_clicked = !last_action_button_state && new_action_button_state;
    last_action_button_state = new_action_button_state;
    if (button_clicked) {
      printf(F("\n"));
      if (isDebugMode()) {
        printf(F("# Button pressed\n"));
      }
      // Reenter the state. This also resets the accomulated charge and time.
      StateReporting::enter();
      return;  
    }
  }
  
  // If we don't have the first reading, read the charge tick and initialize minor slots, major slots and 
  // accomulated data.
  if (!has_last_reading) {
    if (!ltc2943::readAccumCharge(&last_minor_slot_charge_ticks_reading)) {
      printf(F("# LTC2943 charge reading failed (1)\n"));
      StateError::enter();
      return;
    }
    has_last_reading = true;
    last_minor_slot_time_millis = millis();
    slot_tracker.ResetAll();

    if (isDebugMode()) {
      printf(F("# State: REPORTING.1\n"));
    }
    return;
  }
  
  // Here when successive reading. Check if the current minor slot is over.
  // NOTE: the time check below should handle correctly 52 days wraparound of the uint32
  // time in millis.
  const int32 millis_in_current_minor_slot = millis() - last_minor_slot_time_millis;
  if (millis_in_current_minor_slot < analysis::kMillisPerMinorSlot) {
    return;
  }
  
  // NOTE: we keedp the nominal reporting rate. Jitter in the reporting time will not 
  // create an accmulating errors in the reporting charge since we map the charge to
  // current using the nominal reporting rate as used by the consumers of this data.
  last_minor_slot_time_millis += analysis::kMillisPerMinorSlot;
  
  // Read and compute the charge ticks in this minor slot.
  uint16 this_minor_slot_charge_ticks_reading;
  if (!ltc2943::readAccumCharge(&this_minor_slot_charge_ticks_reading)) {
      printf(F("# LTC2943 charge reading failed (2)\n"));
      StateError::enter();
      return;
  }
  // NOTE: this should handle correctly charge register wraps around.
  const uint16 charge_ticks_in_this_minor_slot = 
      (this_minor_slot_charge_ticks_reading - last_minor_slot_charge_ticks_reading);
  last_minor_slot_charge_ticks_reading = this_minor_slot_charge_ticks_reading;

  // Update slot data
  slot_tracker.AddMinorSlot(charge_ticks_in_this_minor_slot);

  // If not the last minor slot in the current major slot than we are done.
  const Mode& selected_mode = modes_table[selected_mode_index];
  if (slot_tracker.minor_slots_in_current_major_slot < selected_mode.minor_slots_per_major_slot) {
    return;
  }
  
  // Compute major slot values.
  analysis::ChargeResults major_slot_charge_results;
  analysis::ComputeChargeResults(slot_tracker.major_slot_charge_tracker, &major_slot_charge_results);
  analysis::PrintablePpmValue major_slot_amps_printable(major_slot_charge_results.average_current_micro_amps);

  // Compute total values.
  analysis::ChargeResults total_charge_results;
  analysis::ComputeChargeResults(slot_tracker.total_charge_tracker, &total_charge_results); 
  analysis::PrintablePpmValue total_charge_amp_hour_printable(total_charge_results.charge_micro_amps_hour);
  analysis::PrintablePpmValue total_average_current_amps_printable(total_charge_results.average_current_micro_amps); 

  analysis::PrintableMilsValue timestamp_secs_printable(slot_tracker.total_charge_tracker.time_millis);

  leds::activity.action(); 
  
  const uint8 format = selected_mode.format;
  
  if (format == formats::kTimeVsCurrent) {
    printf(F("%05u.%03u %u.%06lu\n"), 
        timestamp_secs_printable.units,  timestamp_secs_printable.mils,
        major_slot_amps_printable.units, major_slot_amps_printable.ppms);  
   } else if (format == formats::kDetailed) {
    // TODO: Increase the sio buffer size so we can printf in one statement.
    printf(F("%u.%03u %u.%03u %u.%03u %u.%03u"), 
        timestamp_secs_printable.units,  timestamp_secs_printable.mils, 
        major_slot_amps_printable.units, 
        major_slot_amps_printable.mils, 
        total_charge_amp_hour_printable.units, 
        total_charge_amp_hour_printable.mils,
        total_average_current_amps_printable.units, 
        total_average_current_amps_printable.mils); 
    printf(F(" %lu %lu %lu %d\n"), 
        slot_tracker.standby_minor_slots_charge_tracker.time_millis,
        slot_tracker.awake_minor_slots_charge_tracker.time_millis,
        slot_tracker.total_awakes,
        slot_tracker.awake_minor_slots_in_current_major_slot); 
  } else if (format == formats::kDetailedLabeled) {
    // TODO: Increase the sio buffer size so we can printf in one statement.
    printf(F("T=[%u.%03u]  I=[%u.%03u]  Q=[%u.%03u]  IAv=[%u.%03u]"), 
        timestamp_secs_printable.units, timestamp_secs_printable.mils, 
        major_slot_amps_printable.units, 
        major_slot_amps_printable.mils, 
        total_charge_amp_hour_printable.units, 
        total_charge_amp_hour_printable.mils,
        total_average_current_amps_printable.units, 
        total_average_current_amps_printable.mils); 
    printf(F("  TSB=[%lu]  TAW=[%lu]  #AW=[%lu]%s\n"), 
        slot_tracker.standby_minor_slots_charge_tracker.time_millis/ 1000,
        slot_tracker.awake_minor_slots_charge_tracker.time_millis / 1000,
        slot_tracker.total_awakes,
        (slot_tracker.awake_minor_slots_in_current_major_slot ? " *" : "")); 
  } else if (format == formats::kDebug) {
    printf(F("0x%4x %4u | %6lu | %6lu %6lu %6lu %9lu\n"), 
        this_minor_slot_charge_ticks_reading, charge_ticks_in_this_minor_slot, 
        major_slot_charge_results.average_current_micro_amps, 
        slot_tracker.total_charge_tracker.time_millis, slot_tracker.total_charge_tracker.charge_ticks, 
        total_charge_results.charge_micro_amps_hour, total_charge_results.average_current_micro_amps);
  } else {
    printf(F("Unknown format: %d\n"), format); 
  }
  
  // Reset the major slot data for the next slot.
  slot_tracker.ResetMajorSlot();
}

inline void StateError::enter() {
  state = states::ERROR;
  if (isDebugMode()) {
    printf(F("# State: ERROR\n"));
  }
  time_in_state.restart();  
  leds::errors.action();
}

inline void StateError::loop() {
  // Insert a short delay to avoid flodding teh serial output with error messages
  // in case we have a permanent error condition.
  if (time_in_state.timeMillis() < 1000) {
    return;
  }
  // Try again from scratch.
  StateInit::enter();
}

// Arduino loop() method. Called after setup(). Never returns.
// This is a quick loop that does not use delay() or other 'long' busy loops
// or blocking calls. typical iteration is ~ 50usec with 16Mhz CPU.
void loop() {
  // Call the loop() function of the underlying modules.
  config::loop();  
  buttons::loop();
  leds::loop(); 
  
  switch (state) {
    case states::INIT:
      StateInit::loop();
      break;
    case states::REPORTING:
      StateReporting::loop();
      break;
    case states::ERROR:
      StateError::loop();
      break;
    default:
      printf(F("# Unknown state: %d\n"), state);
      StateError::enter();
      break;  
  }
}

