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

// NOTE: to examine the static RAM used by this program run 
// avr-size -C --mcu=atmega328p  <build_output_path>/arduino.cpp.elf

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
#include "button.h"
#include "display.h"
#include "display_messages.h"
#include "ltc2943.h"
#include "passive_timer.h"

namespace display_page {
  // Page ids.
  static const uint8 kGraphPage = 1;
  static const uint8 kSummary1Page = 2; 
  static const uint8 kSummary2Page = 3; 
 
  // Pages metadata
  static const uint8 kDefaultPage = kGraphPage;
  static const uint8 kMinPage = kGraphPage;
  static const uint8 kMaxPage = kSummary2Page;
}

// Current display page. One of display_page:: values.
// Not used when in test mode.
static uint8 selected_display_page;

// Indicates if the unit is in test mode (used for post manufacturing tests). The test
// mode is activated by pressing the button while turning the unit on. To exist the
// test mode power cycle the unit (without having the button pressed).
static boolean is_in_test_mode;

// Not used when in test mode.
// Increment selected_display_page to the next page.
static inline void incrementCurrentDisplayPage() {
  // Assuming upon entry value is valid.
  if (++selected_display_page > display_page::kMaxPage) {
    selected_display_page = display_page::kMinPage;
  }
}

// Output pin for debugging.
io_pins::OutputPin debug_pin(PORTD, 4);

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
    // Before starting the first slot we first perform an initial reading
    // of the charge counter. This point in time is the 0 time mark of this analysis. 
    static bool has_last_reading;
       
    static uint32 last_slot_time_millis;
    static uint16 last_slot_charge_ticks_reading;
    
    // Used to track the various slots data.
    static analysis::SlotTracker slot_tracker;
};

bool StateReporting::has_last_reading;
uint32 StateReporting::last_slot_time_millis;
uint16 StateReporting::last_slot_charge_ticks_reading;  
analysis::SlotTracker StateReporting::slot_tracker;

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
  
  printf(F("\n"));

  button::setup();
  
  // Wait until button debouncer stabilizes and check if pressed. This will
  // activate the test mode.
  //
  // TODO: skip the debouncer stabilization and just read the button pin once. This will
  // speedup the setup.
  while (!button::hasStableValue()) {
    button::loop();
  }
  is_in_test_mode = button::isButtonPressed();
  
  // Setup display.
  display::setup();
  selected_display_page = display_page::kDefaultPage;

  // Initialize the LTC2943 driver and I2C library.
  // TODO: move this to state machine, check error code.
  ltc2943::setup();
  
  StateInit::enter();
 
  // Enable global interrupts.
  sei(); 
  
  if (is_in_test_mode) {
    display::showMessage(display_messages::code::kTestMode, 2000);
  } else {
    display::showMessage(display_messages::code::kSplashScreen, 2500);
  }
}

void StateInit::enter() {
  state = states::INIT;
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
}

void StateReporting::loop() {
  debug_pin.high();
  debug_pin.low();
  
  // Handle button events. We ignore it if in test mode.
  if (!is_in_test_mode) {
    const uint8 button_event = button::consumeEvent();
    // Short click- TBD
    if (button_event == button::event::kClick) {
       incrementCurrentDisplayPage();
    }
    // Long press - reset the analysis
    else if (button_event == button::event::kLongPress) {
       printf(F("\n"));
       // Reenter the state. This also resets the accumulated charge and time.
       StateReporting::enter();
       display::showMessage(display_messages::code::kAnalysisReset, 750);
       return;  
    }
  }
  
  // If we don't have the first reading, read the charge tick register and initialize
  // the analysis.
  if (!has_last_reading) {
    if (!ltc2943::readAccumCharge(&last_slot_charge_ticks_reading)) {
      printf(F("# LTC2943 charge reading failed (1)\n"));
      StateError::enter();
      return;
    }
    has_last_reading = true;
    last_slot_time_millis = millis();
    slot_tracker.ResetAll();
    
    display::clearGraphBuffer();
    return;
  }
  
  // Check if this time slot is over.
  // NOTE: the time check below should handle correctly 52 days wraparound of the uint32
  // time in millis.
  const int32 millis_in_current_slot = millis() - last_slot_time_millis;
  if (millis_in_current_slot < analysis::kMillisPerSlot) {
    return;
  }
  
  // NOTE: we keep the nominal reporting rate. Jitter in the reporting time will not 
  // create an accumulating errors in the reporting charge since we map the charge to
  // current using the nominal reporting rate as used by the consumers of this data.
  last_slot_time_millis += analysis::kMillisPerSlot;
  
  // Read and compute the charge ticks in this slot.
  uint16 this_slot_charge_ticks_reading;
  if (!ltc2943::readAccumCharge(&this_slot_charge_ticks_reading)) {
      printf(F("# LTC2943 charge reading failed (2)\n"));
      StateError::enter();
      return;
  }
  // NOTE: this should handle correctly charge register wraps around.
  const uint16 charge_ticks_in_this_slot = 
      (this_slot_charge_ticks_reading - last_slot_charge_ticks_reading);
  last_slot_charge_ticks_reading = this_slot_charge_ticks_reading;

  // Update analysis with slot data.
  slot_tracker.AddSlot(charge_ticks_in_this_slot);

  // Read also the current voltage
  uint16 voltage_raw_register_value;
  uint16 voltage_mv;
  if (!ltc2943::readVoltage(&voltage_raw_register_value, &voltage_mv)) {
    printf(F("# LTC2943 voltage reading failed\n"));
    StateError::enter();
    return;
  }
  const analysis::PrintableMilsValue printable_voltage(voltage_mv);
  
  // Compute last slot values.
  analysis::ChargeResults last_slot_charge_results;
  analysis::ComputeChargeResults(slot_tracker.last_slot_charge_tracker, &last_slot_charge_results);
  analysis::PrintablePpmValue last_slot_amps_printable(last_slot_charge_results.average_current_micro_amps);
  const uint16 last_slot_current_millis = last_slot_charge_results.average_current_micro_amps / 1000;
  
  // Compute super slot values.
  analysis::ChargeResults prev_super_slot_charge_results;
  analysis::ComputeChargeResults(slot_tracker.prev_super_slot_charge_tracker, &prev_super_slot_charge_results);
  //analysis::PrintablePpmValue prev_super_slot_amps_printable(prev_super_slot_charge_results.average_current_micro_amps);
  
  analysis::ChargeResults prev_super_slot_total_charge_results;
  analysis::ComputeChargeResults(slot_tracker.total_charge_tracker_at_prev_super_slot, &prev_super_slot_total_charge_results); 
  
  // Compute total values in this analyais. 
  analysis::ChargeResults total_charge_results;
  analysis::ComputeChargeResults(slot_tracker.total_charge_tracker, &total_charge_results); 
  analysis::PrintablePpmValue total_charge_amp_hour_printable(total_charge_results.charge_micro_amps_hour);
  analysis::PrintablePpmValue total_average_current_amps_printable(total_charge_results.average_current_micro_amps); 

  analysis::PrintableMilsValue timestamp_secs_printable(slot_tracker.total_charge_tracker.time_millis);
  
  analysis::ChargeResults wake_slots_charge_results;
  analysis::ComputeChargeResults(slot_tracker.wake_slots_charge_tracker, &wake_slots_charge_results);
  
  // Append the last slot current to the display graph buffer.
  display::appendGraphPoint(last_slot_current_millis);
  
  // Render the current display page.
  if (is_in_test_mode) {
    display::renderTestPage(printable_voltage, last_slot_amps_printable, 
        this_slot_charge_ticks_reading, button::isButtonPressed());
  } else if (selected_display_page == display_page::kGraphPage) {
    display::renderGraphPage(prev_super_slot_charge_results.average_current_micro_amps, 
        prev_super_slot_total_charge_results.average_current_micro_amps);
  } else if (selected_display_page == display_page::kSummary1Page) {
    //const uint16 current_millis = major_slot_charge_results.average_current_micro_amps / 1000;
    const uint16 total_charge_milli_amp_hour = total_charge_results.charge_micro_amps_hour / 1000;
    display::renderSummary1Page(
        prev_super_slot_charge_results.average_current_micro_amps, 
        prev_super_slot_total_charge_results.average_current_micro_amps, 
        total_charge_milli_amp_hour, timestamp_secs_printable.units);     
  } else if (selected_display_page == display_page::kSummary2Page) {
    display::renderSummary2Page(
        printable_voltage, 
        slot_tracker.last_slot_was_wake,
        slot_tracker.total_wakes,
        wake_slots_charge_results.charge_micro_amps_hour / 1000,
        timestamp_secs_printable.units);        
  } else {
    display::showMessage(display_messages::code::kGeneralError, 100);
  }
  
  // TODO: Increase the sio buffer size so we can printf in one statement.
  printf(F("%u.%03u %u.%03u %u.%03u %u.%03u"), 
      timestamp_secs_printable.units,  timestamp_secs_printable.mils, 
      last_slot_amps_printable.units, 
      last_slot_amps_printable.mils, 
      total_charge_amp_hour_printable.units, 
      total_charge_amp_hour_printable.mils,
      total_average_current_amps_printable.units, 
      total_average_current_amps_printable.mils); 
  printf(F(" %lu %lu %lu %u\n"), 
      slot_tracker.standby_slots_charge_tracker.time_millis,
      slot_tracker.wake_slots_charge_tracker.time_millis,
      slot_tracker.total_wakes,
      (slot_tracker.last_slot_was_wake ? 1 : 0)); 
}

inline void StateError::enter() {
  state = states::ERROR;
  time_in_state.restart(); 
  // NOTE: to trigger this error for testing, short the 8.2k resistor between the 
  // device (+) output and the voltage adjustment potentiometer. 
  // This will reduce the output voltage to ~1.5V and will make the LTC2942 non responsive. 
  display::showMessage(display_messages::code::kLtc2943InitError, 1500);
}

inline void StateError::loop() {
  // Insert a short delay to avoid flooding the serial output with error messages
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
  button::loop();
  
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

