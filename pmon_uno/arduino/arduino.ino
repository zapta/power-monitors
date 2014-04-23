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
#include "hardware_clock.h"
#include "leds.h"
#include "ltc2943.h"
#include "passive_timer.h"
#include "sio.h"
#include "system_clock.h"

// IMPORTANT: when performing the various const calculations below, make sure not to introduce
// truncation errors for example by integer division.


// The charge in pico amps / hour per a single charge tick from the LTC2943. Based on the
// formula in the LTC2943's datasheet and the following board specifics
// * Shunt resistor: 25 millohms.
// * Columb counter prescaler : 1
static const uint32 kChargePerTickPicoAmpHour = 166016L;

// Given:
//   charge_ticks - number of charge ticks within a time period.
//   time_millis - the time period in millis
// Return:
//   charge_micro_amps_hour - the total charge in micro amps hour.
//   avg_current_micro_amps - the average current in that period in micro amps.
static inline void convert(uint32 charge_ticks, uint32 time_millis, 
    uint32* charge_micro_amps_hour, uint32* avg_current_micro_amps) {
  const uint64 total_charge_pico_amps_hour =  ((uint64)kChargePerTickPicoAmpHour) * charge_ticks;
  *charge_micro_amps_hour = (uint32)(total_charge_pico_amps_hour / 1000000L);
  *avg_current_micro_amps = (uint32)((total_charge_pico_amps_hour * 3600 / 1000) / time_millis);
}

// Represent the parameters of a measurement mode.
struct Mode {
  const bool report_total_charge;
  const uint16 reporting_time_millis;

  Mode(bool report_total_charge, uint16 reporting_time_millis)
  : 
   report_total_charge(report_total_charge),
   reporting_time_millis(reporting_time_millis) {
  }
};

// Mode table. Indexed by config::modeIndex();
static const Mode modes[] = {
  Mode(false, 1000),
  Mode(false, 100),
  Mode(true, 1000),
  Mode(true, 100),
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
    static bool has_last_reading;
    static uint32 last_report_time_millis;
    static uint16 last_report_charge_reading;
    // For accomulated reporting.
    static uint32 accomulated_charge_ticks;
    static uint32 reporting_start_time_millis;
    // For restart button press detection
    static bool last_action_button_state;
};
uint8 StateReporting::selected_mode_index;
bool StateReporting::has_last_reading;
uint32 StateReporting::last_report_time_millis;
uint16 StateReporting::last_report_charge_reading;
uint32 StateReporting::accomulated_charge_ticks;
uint32 StateReporting::reporting_start_time_millis;
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
    if (config::isDebug()) {
      sio::printf(F("# device init failed\n"));
    }
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
    // The switch is done when reentering the state. This provides graceful 
    // transition.
    StateReporting::enter();
    return;
  }
  
  // If button pressed (low to high transition, then reenter state.
  {
    const bool new_action_button_state = buttons:: isActionButtonPressed();
    const bool button_click = !last_action_button_state && new_action_button_state;
    last_action_button_state = new_action_button_state;
    if (button_click) {
      sio::println();
      if (config::isDebug()) {
        sio::printf(F("# Button pressed\n"));
      }
      // Reenter the state. This also resets the accomulated charge and time.
      StateReporting::enter();
      return;  
    }
  }
  
  // Try first reading.
  if (!has_last_reading) {
    if (!ltc2943::readAccumCharge(&last_report_charge_reading)) {
      if (config::isDebug()) {
        sio::printf(F("# First reading failed\n"));
      }
      StateError::enter();
      return;
    }
    last_report_time_millis = system_clock::timeMillis();
    has_last_reading = true;
    accomulated_charge_ticks = 0;
    reporting_start_time_millis = last_report_time_millis;
    if (config::isDebug()) {
      sio::printf(F("# State: REPORTING.1\n"));
    }
    return;
  }
  
  // Here when successive reading. If not time yet to next reading do nothing.
  // NOTE: the time check below should handle correctly 52 days wraparound of the uint32
  // time in millis.
  const Mode& selected_mode = modes[selected_mode_index];
  const uint16 current_reporting_time_millis = selected_mode.reporting_time_millis;
  
  const uint32 time_now_millis = system_clock::timeMillis();
  const int32 time_diff_millis = time_now_millis - last_report_time_millis;
  if (time_diff_millis < current_reporting_time_millis) {
    return;
  }
  
  // NOTE: we keedp the nominal reporting rate. Jitter in the reporting time will not 
  // create an accmulating errors in the reporting charge since we map the charge to
  // current using the nominal reporting rate as used by the consumers of this data.
  last_report_time_millis += current_reporting_time_millis;
  
  // Do the successive reading.
  uint16 current_report_charge_reading;
  if (!ltc2943::readAccumCharge(&current_report_charge_reading)) {
      if (config::isDebug()) {
        sio::printf(F("# Charge reading failed\n"));
      }
      StateError::enter();
      return;
  }
  
  // NOTE: this should handle correctly charge register wraps around.
  const uint16 carge_ticks_in_period = (current_report_charge_reading - last_report_charge_reading);
  last_report_charge_reading = current_report_charge_reading;

  uint32 period_charge_micro_amps_hour;
  uint32 period_average_current_micro_amps;
  convert(carge_ticks_in_period, selected_mode.reporting_time_millis, &period_charge_micro_amps_hour, &period_average_current_micro_amps);
  
  accomulated_charge_ticks += carge_ticks_in_period;
  const uint32 accomulated_time_millis = time_now_millis - reporting_start_time_millis;
  
  uint32 total_charge_micro_amps_hour;
  uint32 total_average_current_micro_amps;
  convert(accomulated_charge_ticks, accomulated_time_millis, &total_charge_micro_amps_hour, &total_average_current_micro_amps);
    
  // Convert to ints for printouts
  const uint16 period_amps = period_average_current_micro_amps / 1000000L;
  const uint32 period_micro_amps = period_average_current_micro_amps - (period_amps * 1000000L);
  
  // Convert to ints for printout
  const uint16 total_amps_hour = total_charge_micro_amps_hour / 1000000L;
  const uint32 total_micro_amps_hour = total_charge_micro_amps_hour - (total_amps_hour * 1000000L);
  
  // Convert to ints for printout
  const uint16 total_amps = total_average_current_micro_amps / 1000000L;
  const uint32 total_micro_amps = total_average_current_micro_amps - (total_amps * 1000000L);
 
  
  if (config::isDebug()) {
    sio::printf(F("0x%4x %4u | %6lu | %6lu %6lu %6lu %9lu\n"), 
        last_report_charge_reading, carge_ticks_in_period, 
        period_average_current_micro_amps, 
        accomulated_time_millis, accomulated_charge_ticks, total_charge_micro_amps_hour, total_average_current_micro_amps);
  } else if (selected_mode.report_total_charge) {
     sio::printf(F("T: %08lu   I: %u.%06lu   Q: %u.%06lu   IAv: %u.%06lu\n"), 
         accomulated_time_millis,  period_amps, period_micro_amps, 
         total_amps_hour, total_micro_amps_hour, total_amps, total_micro_amps);  
  } else {
    sio::printf(F("%08lu %d.%06ld\n"), accomulated_time_millis, period_amps, period_micro_amps);  
  }
  leds::activity.action(); 
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
  if (time_in_state.timeMillis() < 100) {
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
        if (config::isDebug()) {
          sio::printf(F("# Unknown state: %d\n"), state);
        }
        StateError::enter();
        break;  
    }
  }
}


