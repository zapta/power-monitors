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

#include "analysis.h"

namespace analysis {
  
  void ComputeChargeResults(const ChargeTracker& charge_tracker, ChargeResults* results) {
    const uint64 total_charge_pico_amps_hour =  
        ((uint64)kChargePerTickPicoAmpHour) * charge_tracker.charge_ticks;
        
    results->charge_micro_amps_hour = 
        (uint32)(total_charge_pico_amps_hour / 1000000L);
        
    results->average_current_micro_amps = 
        (uint32)((total_charge_pico_amps_hour * 3600 / 1000) / charge_tracker.time_millis);
  }
}

