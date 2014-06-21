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

#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "avr_util.h"

// Data and conversions related utilities.
namespace analysis {
  
// The charge in pico amps / hour per a single charge tick from the LTC2943. Based on the
// formula in the LTC2943's datasheet and the following board specifics
// * Shunt resistor: 25 milliohms.
// * Coulomb counter prescaler : 1
static const uint32 kChargePerTickPicoAmpHour = 166016L;

// A slot with at least this number of charge ticks is considered to 
// be a wake. Otherwise, a standby.
// With 25mhoms shunt, prescaler of 1 and 100ms slot, each unit here
// represents about 5.976ma. Note that the accuracy is +/- 1 tick so this 
// number should not be too small (alternatively, increase the length of
// the slot).
static const uint8 kMinChargeTicksPerWakeSlot = 10;

// The basic measurement period, in millis.
static const uint16 kMillisPerSlot = 100;

// We display the average momentary current every this number of slots.
//
// TODO: make this private.
static const uint8 kSlotsInSuperSlot = 10;

// Data container for tracking charge over a time period. 
struct ChargeTracker {
  // TODO: consider to count slots rather than milliseconds.
  uint32 time_millis;
  uint32 charge_ticks;
  
  inline void Reset() {
    time_millis = 0;
    charge_ticks = 0;
  } 
  
  // NOTE: the detlas are limited to 16 bits which is sufficient in our use case.
  inline void AddSmallCharge(uint16 delta_time_millis, uint16 delta_charge_ticks) {
    time_millis += delta_time_millis;
    charge_ticks += delta_charge_ticks;
  }
  
  inline void CopyFrom(const ChargeTracker& other) {
    time_millis = other.time_millis;
    charge_ticks = other.charge_ticks;
  }
};

// The data of a ChargeTracker mapped to useful units.
struct ChargeResults {
  // The charge in micro amp hours.
  uint32 charge_micro_amps_hour;
  // The average current during the charge tracking period.
  uint32 average_current_micro_amps;
};

// A breakup of a fixed point value into the integer part and the
// parts-per-million part. Use to print fixed point number in the
// absense of printf support for floats.
struct PrintablePpmValue {
  const uint16 units;
  const uint32 ppms;
  const uint16 mils; 
  
  inline PrintablePpmValue(uint32 value_ppms) 
  :
    units(value_ppms / 1000000L),
    ppms(value_ppms - (units * 1000000L)),
    mils((ppms + 500) / 1000) {
  } 
};

// Same as PrintablePpmValue but input value is in mils.
struct PrintableMilsValue {
  const uint16 units;
  const uint16 mils; 
  
  inline PrintableMilsValue(uint32 value_mils) 
  :
    units(value_mils / 1000L),
    mils(value_mils - (units * 1000L)) {
  } 
};

// Tracks the various slots data. 
class SlotTracker {
 public:
  // Single slot data.
  ChargeTracker last_slot_charge_tracker;
  boolean last_slot_was_wake;
  
  // Super slot data. 
  ChargeTracker prev_super_slot_charge_tracker;
  ChargeTracker current_super_slot_charge_tracker;
  uint8 num_slots_in_current_super_slot;
  // Total charge at the end of previous super slot. Used to display the total charge
  // at same interval as previous slot current.
  ChargeTracker total_charge_tracker_at_prev_super_slot; 
  
  // Analysis total data.
  ChargeTracker total_charge_tracker; 
  ChargeTracker standby_slots_charge_tracker;
  ChargeTracker wake_slots_charge_tracker;
  uint32 total_standby_slots;
  uint32 total_wake_slots;
  // Number of transitions from a standby slot to an wake slot.
  uint32 total_wakes;

  SlotTracker() {
  }
  
  inline void ResetAll() {
    // Slot data.
    last_slot_charge_tracker.Reset();
    last_slot_was_wake = false;
    
    // Super slot data.
    prev_super_slot_charge_tracker.Reset();
    current_super_slot_charge_tracker.Reset();
    num_slots_in_current_super_slot = 0;
    total_charge_tracker_at_prev_super_slot.Reset();
    
    // Total analysis data.
    total_charge_tracker.Reset();
    standby_slots_charge_tracker.Reset();
    wake_slots_charge_tracker.Reset();

    // The sum of standby and wake slot is the total number of slots in this analysis.
    total_standby_slots = 0;
    total_wake_slots = 0;
    total_wakes = 0;
  }
  
  inline void AddSlot(uint16 charge_ticks) {   
    const boolean is_wake_slot = charge_ticks >= kMinChargeTicksPerWakeSlot;
    const boolean is_wake_count = is_wake_slot && !last_slot_was_wake;
    
    // TODO: add a Init() method instead of reset + add.
    last_slot_charge_tracker.Reset();
    last_slot_charge_tracker.AddSmallCharge(kMillisPerSlot, charge_ticks);
    last_slot_was_wake = is_wake_slot;
    
    total_charge_tracker.AddSmallCharge(kMillisPerSlot, charge_ticks); 
    
    current_super_slot_charge_tracker.AddSmallCharge(kMillisPerSlot, charge_ticks);
    num_slots_in_current_super_slot++;
    if (num_slots_in_current_super_slot >= kSlotsInSuperSlot) {
      prev_super_slot_charge_tracker.CopyFrom(current_super_slot_charge_tracker);
      //prev_super_slot_charge_tracker.Reset();
      // TODO: add a Init() method instead of reset + add.
      //prev_super_slot_charge_tracker.AddCharge(current_super_slot_charge_tracker.time_millis, current_super_slot_charge_tracker.charge_ticks);
      current_super_slot_charge_tracker.Reset();
      num_slots_in_current_super_slot = 0;
      
      //total_charge_tracker_at_prev_super_slot.Reset();
      //total_charge_tracker_at_prev_super_slot.AddCharge(total_charge_tracker.time_millis, total_charge_tracker.charge_ticks);
      total_charge_tracker_at_prev_super_slot.CopyFrom(total_charge_tracker);
    }
        
    if (is_wake_slot) {
      wake_slots_charge_tracker.AddSmallCharge(kMillisPerSlot, charge_ticks);
      total_wake_slots++;
      if (is_wake_count) {
        total_wakes++;
      }
      //wake_minor_slots_in_current_major_slot++;
    } else {
      standby_slots_charge_tracker.AddSmallCharge(kMillisPerSlot, charge_ticks);
      total_standby_slots++;
    }
  }
};

// Convert a raw tracked charge into useful units.
extern void ComputeChargeResults(const ChargeTracker& charge_tracker, ChargeResults* results);

}  // namespace

#endif
