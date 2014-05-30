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
#include "button.h"
#include "config.h"
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
static uint8 selected_display_page;

// Set during reset if the button is pressed. Used to activate the test
// page on boards without the dip switches. Reset when the user exists the
// test page.
static boolean test_page_on_reset_active;

static inline void incrementCurrentDisplayPage() {
  if (test_page_on_reset_active) {
    test_page_on_reset_active = false;
    return;  
  }
  
  // Assuming upon entry value is valid.
  if (++selected_display_page > display_page::kMaxPage) {
    selected_display_page = display_page::kMinPage;
  }
  //printf(F("Incremented page: %u\n"), selected_display_page);
}

// Output pin for debugging.
io_pins::OutputPin debug_pin(PORTD, 4);

namespace formats {
  static const uint8 kTimeVsCurrent = 1;
  static const uint8 kDetailed = 2;
  static const uint8 kDetailedLabeled = 3;
  static const uint8 kDebug = 4;
}

// Represent the parameters of a measurement mode.
struct Mode {
  const uint8 format;
  // format is one of format:* values.
  Mode(uint8 format)
  : 
   format(format) {
  }
};

// Mode table. Indexed by config::modeIndex();
static const Mode modes_table[] = {
  // NOTE: this is the most useful reporting mode so we set it as mode zero such
  // that boards without the config dip switch will report in this mode.
  //
  // 0 - Labeled detailed format.
  Mode(formats::kDetailedLabeled),
  // 1 - Unlabeled detailed format.
  Mode(formats::kDetailed),
  // 2 - Basic format.
  Mode(formats::kTimeVsCurrent),
  // 3 - Unlabeled detailed format.
  Mode(formats::kDebug),
};

static inline boolean isDebugMode() {
  // TODO: define a const.
  return config::modeIndex() == 3;
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
  
  if (isDebugMode()) {
    printf(F("\nStarted\n"));
  } else {
    printf(F("\n"));
  }

  config::setup();
  button::setup();

  
  // Wait until config and button decouncer stalize.
  while (!config::hasStableValue() || !button::hasStableValue()) {
    config::loop();
    button::loop();
  }
  
  // If button is pressed upon restart we will show first the 
  // test page.
  test_page_on_reset_active = button::isButtonPressed();
  
  // Setup display.
  display::setup();
  selected_display_page = display_page::kDefaultPage;

  // Initialize the LTC2943 driver and I2C library.
  // TODO: move this to state machine, check error code.
  ltc2943::setup();
  
  StateInit::enter();
 
  // Enable global interrupts.
  sei(); 
  
  if (test_page_on_reset_active) {
    display::showMessage(display_messages::code::kTestMode, 1000);
  } else {
    display::showMessage(display_messages::code::kSplashScreen, 2500);
  }
}

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
}

void StateReporting::loop() {
  debug_pin.high();
  debug_pin.low();

  const uint8 selected_mode_index = config::modeIndex();
  
  // Handle button events.
  {
    const uint8 button_event = button::consumeEvent();
    // Short click- TBD
    if (button_event == button::event::kClick) {
       incrementCurrentDisplayPage();
    }
    // Long press - reset the analysis
    else if (!test_page_on_reset_active && button_event == button::event::kLongPress) {
       printf(F("\n"));
       // Reenter the state. This also resets the accomulated charge and time.
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

    if (isDebugMode()) {
      printf(F("# State: REPORTING.1\n"));
    }
    return;
  }
  
  // Check if thsi time slot is over.
  // NOTE: the time check below should handle correctly 52 days wraparound of the uint32
  // time in millis.
  const int32 millis_in_current_slot = millis() - last_slot_time_millis;
  if (millis_in_current_slot < analysis::kMillisPerSlot) {
    return;
  }
  
  // NOTE: we keedp the nominal reporting rate. Jitter in the reporting time will not 
  // create an accmulating errors in the reporting charge since we map the charge to
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

  // Update slot data
  slot_tracker.AddSlot(charge_ticks_in_this_slot);

  // Get the current reporting mode.
  const Mode& selected_mode = modes_table[selected_mode_index];
  
  // Read also the current voltage
  uint16 voltage_raw_register_value;
  uint16 voltage_mv;
  if (!ltc2943::readVoltage(&voltage_raw_register_value, &voltage_mv)) {
    printf(F("# LTC2943 volage reading failed\n"));
    StateError::enter();
    return;
  }
  const analysis::PrintableMilsValue printable_voltage(voltage_mv);
  
  // Compute last slot values.
  analysis::ChargeResults last_slot_charge_results;
  analysis::ComputeChargeResults(slot_tracker.last_slot_charge_tracker, &last_slot_charge_results);
  analysis::PrintablePpmValue last_slot_amps_printable(last_slot_charge_results.average_current_micro_amps);
  // Compute total values in this analyais. 
  analysis::ChargeResults total_charge_results;
  analysis::ComputeChargeResults(slot_tracker.total_charge_tracker, &total_charge_results); 
  analysis::PrintablePpmValue total_charge_amp_hour_printable(total_charge_results.charge_micro_amps_hour);
  analysis::PrintablePpmValue total_average_current_amps_printable(total_charge_results.average_current_micro_amps); 

  analysis::PrintableMilsValue timestamp_secs_printable(slot_tracker.total_charge_tracker.time_millis);
  
  analysis::ChargeResults wake_slots_charge_results;
  analysis::ComputeChargeResults(slot_tracker.wake_slots_charge_tracker, &wake_slots_charge_results);
  
  const uint16 current_millis = last_slot_charge_results.average_current_micro_amps / 1000;
  display::appendGraphPoint(current_millis);

  // Render the current display page.
  if (test_page_on_reset_active || config::isTestMode()) {
    display::renderTestPage(printable_voltage, last_slot_amps_printable, config::rawDipSwitches(), button::isButtonPressed());
  } else if (selected_display_page == display_page::kGraphPage) {
    const uint16 average_current_millis = total_charge_results.average_current_micro_amps / 1000;
    display::renderGraphPage(current_millis, average_current_millis);
  } else if (selected_display_page == display_page::kSummary1Page) {
    //const uint16 current_millis = major_slot_charge_results.average_current_micro_amps / 1000;
    const uint16 average_current_millis = total_charge_results.average_current_micro_amps / 1000;
    const uint16 total_charge_milli_amp_hour = total_charge_results.charge_micro_amps_hour / 1000;
    display::appendGraphPoint(current_millis);
    display::renderSummary1Page(current_millis, average_current_millis,
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
  
  const uint8 format = selected_mode.format;
  
  if (format == formats::kTimeVsCurrent) {
    printf(F("%05u.%03u %u.%06lu\n"), 
        timestamp_secs_printable.units,  timestamp_secs_printable.mils,
        last_slot_amps_printable.units, last_slot_amps_printable.ppms);  
   } else if (format == formats::kDetailed) {
    // TODO: Increase the sio buffer size so we can printf in one statement.
    printf(F("%u.%03u %u.%03u %u.%03u %u.%03u"), 
        timestamp_secs_printable.units,  timestamp_secs_printable.mils, 
        last_slot_amps_printable.units, 
        last_slot_amps_printable.mils, 
        total_charge_amp_hour_printable.units, 
        total_charge_amp_hour_printable.mils,
        total_average_current_amps_printable.units, 
        total_average_current_amps_printable.mils); 
    printf(F(" %lu %lu %lu %s\n"), 
        slot_tracker.standby_slots_charge_tracker.time_millis,
        slot_tracker.wake_slots_charge_tracker.time_millis,
        slot_tracker.total_wakes,
        (slot_tracker.last_slot_was_wake ? "*" : "_")); 
  } else if (format == formats::kDetailedLabeled) {
    // TODO: Increase the sio buffer size so we can printf in one statement.
    printf(F("T=[%u.%03u]  I=[%u.%03u]  Q=[%u.%03u]  IAv=[%u.%03u]"), 
        timestamp_secs_printable.units, timestamp_secs_printable.mils, 
        last_slot_amps_printable.units, 
        last_slot_amps_printable.mils, 
        total_charge_amp_hour_printable.units, 
        total_charge_amp_hour_printable.mils,
        total_average_current_amps_printable.units, 
        total_average_current_amps_printable.mils); 
    printf(F("  V=[%u.%03u]  TSB=[%lu]  TAW=[%lu]  #AW=[%lu] %s\n"), 
        printable_voltage.units, printable_voltage.mils,
        slot_tracker.standby_slots_charge_tracker.time_millis/ 1000,
        slot_tracker.wake_slots_charge_tracker.time_millis / 1000,
        slot_tracker.total_wakes,
        (slot_tracker.last_slot_was_wake ? "*" : "_")); 
  } else if (format == formats::kDebug) {
    printf(F("0x%4x %4u | %u.%03u | %6lu | %6lu %6lu %6lu %9lu\n"), 
        last_slot_charge_ticks_reading, charge_ticks_in_this_slot, 
        printable_voltage.units, printable_voltage.mils,
        last_slot_charge_results.average_current_micro_amps, 
        slot_tracker.total_charge_tracker.time_millis, slot_tracker.total_charge_tracker.charge_ticks, 
        total_charge_results.charge_micro_amps_hour, total_charge_results.average_current_micro_amps);
  } else {
    printf(F("Unknown format: %d\n"), format); 
  }
}

inline void StateError::enter() {
  state = states::ERROR;
  if (isDebugMode()) {
    printf(F("# State: ERROR\n"));
  }
  time_in_state.restart(); 
  // NOTE: to trigger this error, bypass the 8.2k resistor between the device (+) output
  // and the voltage adjustment potentiometer. This will reduce the output voltage to 
  // ~1.5V and will make the LTC2942 non responsive. 
  display::showMessage(display_messages::code::kLtc2943InitError, 1500);
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

