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

#include "avr_util.h"
#include "buttons.h"
#include "config.h"
#include "data.h"
#include "hardware_clock.h"
#include "leds.h"
#include "ltc2943.h"
#include "passive_timer.h"
#include "sio.h"
#include "system_clock.h"


// Represent the parameters of a measurement mode.
struct Mode {
  // True: generate a detailed report. False: generate a simple <time, current> report.
  const bool is_detailed_report;
  const uint16 minor_slots_per_major_slot;

  Mode(bool is_detailed_report, uint16 minor_slots_per_major_slot)
  : 
   is_detailed_report(is_detailed_report),
   minor_slots_per_major_slot(minor_slots_per_major_slot) {
  }
};

// Mode table. Indexed by config::modeIndex();
static const Mode modes[] = {
  Mode(false, 10),
  Mode(false, 1),
  Mode(true, 10),
  Mode(true, 1),
};

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
    static data::SlotTracker slot_tracker;

        // For restart button press detection
    static bool last_action_button_state;
};

uint8 StateReporting::selected_mode_index;
bool StateReporting::has_last_reading;
uint32 StateReporting::last_minor_slot_time_millis;
uint16 StateReporting::last_minor_slot_charge_ticks_reading;  
data::SlotTracker StateReporting::slot_tracker;
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
void setup()
{
  // Disable timer 0 interrupts. It is setup by the Arduino runtime for the 
  // Arduino time services (which we do not use).
  TCCR0B = 0;
  TCCR0A = 0;

  // Hard coded to 115.2k baud. Uses URART0, no interrupts.
  // Initialize this first since some setup methods uses it.
  sio::setup();
  
  if (config::isDebug()) {
    sio::printf(F("\nStarted\n"));
  } else {
    sio::printf(F("\n"));
  }

  // Uses Timer1, no interrupts.
  hardware_clock::setup();
  
  config::setup();
  
  buttons::setup();
  
  // Let the config value stablizes through the debouncer.
  while (!config::hasStableValue()) {
    system_clock::loop();
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
  if (config::isDebug()) {
    sio::printf(F("# State: INIT\n"));
  }
}

void StateInit::loop() {
  if (ltc2943::init()) {
    StateReporting::enter();
  } else {
    sio::printf(F("# LTC2943 init failed (is power connected?)\n"));
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
  if (config::isDebug()) {
    sio::printf(F("# Mode: %d\n"), selected_mode_index);
    sio::printf(F("# State: REPORTING.0\n"));
  }
}

void StateReporting::loop() {
  // Check for mode change.
  if (config::modeIndex() != selected_mode_index) {
    if (config::isDebug()) {
      sio::printf(F("# Mode changed\n"));
    }
    sio::println();
    // The switch is done when reentering the state. This provides graceful 
    // transition.
    StateReporting::enter();
    return;
  }
  
  // If button pressed (low to high transition, then reenter state.
  {
    const bool new_action_button_state = buttons:: isActionButtonPressed();
    const bool button_clicked = !last_action_button_state && new_action_button_state;
    last_action_button_state = new_action_button_state;
    if (button_clicked) {
      sio::println();
      if (config::isDebug()) {
        sio::printf(F("# Button pressed\n"));
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
      sio::printf(F("# LTC2943 charge reading failed (1)\n"));
      StateError::enter();
      return;
    }
    has_last_reading = true;
    last_minor_slot_time_millis = system_clock::timeMillis();
    slot_tracker.ResetAll();

    if (config::isDebug()) {
      sio::printf(F("# State: REPORTING.1\n"));
    }
    return;
  }
  
  // Here when successive reading. Check if the current minor slot is over.
  // NOTE: the time check below should handle correctly 52 days wraparound of the uint32
  // time in millis.
  const int32 millis_in_current_minor_slot = system_clock::timeMillis() - last_minor_slot_time_millis;
  if (millis_in_current_minor_slot < data::kMillisPerMinorSlot) {
    return;
  }
  
  // NOTE: we keedp the nominal reporting rate. Jitter in the reporting time will not 
  // create an accmulating errors in the reporting charge since we map the charge to
  // current using the nominal reporting rate as used by the consumers of this data.
  last_minor_slot_time_millis += data::kMillisPerMinorSlot;
  
  // Read and compute the charge ticks in this minor slot.
  uint16 this_minor_slot_charge_ticks_reading;
  if (!ltc2943::readAccumCharge(&this_minor_slot_charge_ticks_reading)) {
      sio::printf(F("# LTC2943 charge reading failed (2)\n"));
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
  const Mode& selected_mode = modes[selected_mode_index];
  if (slot_tracker.minor_slots_in_current_major_slot < selected_mode.minor_slots_per_major_slot) {
    return;
  }
  
  // Compute major slot values.
  data::ChargeResults major_slot_charge_results;
  data::ComputeChargeResults(slot_tracker.major_slot_charge_tracker, &major_slot_charge_results);
  data::PrintableValue major_slot_amps_printable(major_slot_charge_results.average_current_micro_amps);

  // Compute total values.
  data::ChargeResults total_charge_results;
  data::ComputeChargeResults(slot_tracker.total_charge_tracker, &total_charge_results); 
  data::PrintableValue total_charge_amp_hour_printable(total_charge_results.charge_micro_amps_hour);
  data::PrintableValue total_average_current_amps_printable(total_charge_results.average_current_micro_amps); 

  leds::activity.action(); 
  if (config::isDebug()) {
    sio::printf(F("0x%4x %4u | %6lu | %6lu %6lu %6lu %9lu\n"), 
        this_minor_slot_charge_ticks_reading, charge_ticks_in_this_minor_slot, 
        major_slot_charge_results.average_current_micro_amps, 
        slot_tracker.total_charge_tracker.time_millis, slot_tracker.total_charge_tracker.charge_ticks, 
        total_charge_results.charge_micro_amps_hour, total_charge_results.average_current_micro_amps);
  } else if (selected_mode.is_detailed_report) {
    // TODO: Increase the sio buffer size so we can printf in one statement.
    sio::printf(F("T=[%lu]  I=[%u.%03u]  Q=[%u.%03u]  IAv=[%u.%03u]"), 
        slot_tracker.total_charge_tracker.time_millis,  
        major_slot_amps_printable.units, 
        major_slot_amps_printable.mils, 
        total_charge_amp_hour_printable.units, 
        total_charge_amp_hour_printable.mils,
        total_average_current_amps_printable.units, 
        total_average_current_amps_printable.mils); 
    sio::printf(F("  TSB=[%lu]  TAW=[%lu]  #AW=[%lu]%s\n"), 
        slot_tracker.standby_minor_slots_charge_tracker.time_millis,
        slot_tracker.awake_minor_slots_charge_tracker.time_millis,
        slot_tracker.total_awakes,
        (slot_tracker.awake_minor_slots_in_current_major_slot ? " *" : "")); 
  } else {
    sio::printf(F("%08lu %d.%06ld\n"), 
        slot_tracker.total_charge_tracker.time_millis,  
        major_slot_amps_printable.units, major_slot_amps_printable.ppms);  
  }  
  
  // Reset the major slot data for the next slot.
  slot_tracker.ResetMajorSlot();
}

inline void StateError::enter() {
  state = states::ERROR;
  if (config::isDebug()) {
    sio::printf(F("# State: ERROR\n"));
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
  // Having our own loop shaves about 4 usec per iteration. It also eliminate
  // any underlying functionality that we may not want.
  for(;;) {
    // Periodic updates.
    system_clock::loop();  
    config::loop();  
    buttons::loop();
    sio::loop();
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
        sio::printf(F("# Unknown state: %d\n"), state);
        StateError::enter();
        break;  
    }
  }
}

