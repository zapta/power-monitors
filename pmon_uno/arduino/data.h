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

#ifndef DATAL_H
#define DATA_H

#include "avr_util.h"

// Data and conversions related utilities.
namespace data {
  
// The charge in pico amps / hour per a single charge tick from the LTC2943. Based on the
// formula in the LTC2943's datasheet and the following board specifics
// * Shunt resistor: 25 millohms.
// * Columb counter prescaler : 1
static const uint32 kChargePerTickPicoAmpHour = 166016L;

// Data container for tracking charge over a time period. 
struct ChargeTracker {
  uint32 time_millis;
  uint32 charge_ticks;
  
  inline void reset() {
    time_millis = 0;
    charge_ticks = 0;
  } 
  
  inline void add(uint32 delta_time_millis, uint32 delta_charge_ticks) {
    time_millis += delta_time_millis;
    charge_ticks += delta_charge_ticks;
  }
};

// The data of a ChargeTracker mapped to usefull units.
struct ChargeResults {
  // The charge in micro amp hours.
  uint32 charge_micro_amps_hour;
  // The average current during the charge tracking period.
  uint32 average_current_micro_amps;
};

// A breakup of a fixed point value into the integer part and the
// parts-per-million part. Use to print fixed point number in the
// absense of printf support for floats.
struct PrintableValue {
  const uint16 units;
  const uint32 ppms; 
  
  inline PrintableValue(uint32 value_ppms) 
  :
    units(value_ppms / 1000000L),
    ppms(value_ppms - (units * 1000000L)) {
  } 
};

// Convert a raw tracked charge into useful units.
extern void ComputeChargeResults(const ChargeTracker& charge_tracker, ChargeResults* results);
}  // namespace

#endif
