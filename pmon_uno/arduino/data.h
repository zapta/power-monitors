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

// A minor slot with at least this number of charge ticks is considered to 
// be awake. Otherwise, a standby.
// With 25mhoms shunt, prescaler of 1 and 100ms minor slot, each unit here
// represents about 5.976ma. Note that the accuracy is +/- 1 tick so this 
// number should not be too small (alternativly, increase the length of
// the minor slot).
static const uint8 kMinAwakeTicksPerMinorSlot = 10;

// The basic measurment period, in millis.
static const uint16 kMillisPerMinorSlot = 100;

// Data container for tracking charge over a time period. 
struct ChargeTracker {
  // TODO: consider to count minor slots rather than milli seconds.
  uint32 time_millis;
  uint32 charge_ticks;
  
  inline void Reset() {
    time_millis = 0;
    charge_ticks = 0;
  } 
  
  inline void AddCharge(uint16 delta_time_millis, uint16 delta_charge_ticks) {
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
  const uint16 mils; 
  
  inline PrintableValue(uint32 value_ppms) 
  :
    units(value_ppms / 1000000L),
    ppms(value_ppms - (units * 1000000L)),
    mils((ppms + 500) / 1000) {
  } 
};

// Tracks the various slots data. 
class SlotTracker {
 public:
  ChargeTracker major_slot_charge_tracker;
  ChargeTracker total_charge_tracker; 
  ChargeTracker standby_minor_slots_charge_tracker;
  ChargeTracker awake_minor_slots_charge_tracker;
  uint32 total_minor_slots;
  uint16 minor_slots_in_current_major_slot;
  uint16 awake_minor_slots_in_current_major_slot;
  uint32 total_standby_minor_slots;
  uint32 total_awake_minor_slots;
  uint32 total_awakes;
  bool last_minor_slot_was_awake;

  SlotTracker() {
  }
  
  inline void ResetAll() {
    major_slot_charge_tracker.Reset();
    total_charge_tracker.Reset();
    standby_minor_slots_charge_tracker.Reset();
    awake_minor_slots_charge_tracker.Reset();
    minor_slots_in_current_major_slot = 0;
    awake_minor_slots_in_current_major_slot = 0;
    total_minor_slots = 0;
    total_standby_minor_slots = 0;
    total_awake_minor_slots = 0;
    total_awakes = 0;
    last_minor_slot_was_awake = false;
  }
  
  inline void AddMinorSlot( uint16 charge_ticks) {
    major_slot_charge_tracker.AddCharge(kMillisPerMinorSlot, charge_ticks);
    total_charge_tracker.AddCharge(kMillisPerMinorSlot, charge_ticks); 
     
    minor_slots_in_current_major_slot++;

    total_minor_slots++;
    
    const bool is_awake = charge_ticks >= kMinAwakeTicksPerMinorSlot;
    if (is_awake) {
      awake_minor_slots_charge_tracker.AddCharge(kMillisPerMinorSlot, charge_ticks);
      total_awake_minor_slots++;
      awake_minor_slots_in_current_major_slot++;
    } else {
      standby_minor_slots_charge_tracker.AddCharge(kMillisPerMinorSlot, charge_ticks);
      total_standby_minor_slots++;
    }
    
    // Count transitions from standby to to awake.
    if (is_awake && ! last_minor_slot_was_awake) {
      total_awakes++;  
    }
    last_minor_slot_was_awake = is_awake;
  }
  
  // Prepare for next major slot.
  inline void ResetMajorSlot() {
    major_slot_charge_tracker.Reset();
    minor_slots_in_current_major_slot = 0;
    awake_minor_slots_in_current_major_slot = 0;
  }
};

// Convert a raw tracked charge into useful units.
extern void ComputeChargeResults(const ChargeTracker& charge_tracker, ChargeResults* results);
}  // namespace

#endif
