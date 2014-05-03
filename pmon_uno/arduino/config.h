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

#ifndef CONFIG_H
#define CONFIG_H

#include "avr_util.h"
#include "byte_debouncer.h"
#include "system_clock.h"

namespace config {
  // Internals.
  namespace private_ {
    extern ByteDebouncer byte_debouncer;
  }

  extern void setup();
  
  extern void loop();
   
  inline bool hasStableValue() {
    return private_::byte_debouncer.hasStableValue();
  }
  
  // Returns a [0,3] index of selected mode (two LSB switches).
  inline uint8 modeIndex() {
    const uint8 config_bits = private_::byte_debouncer.stableValue() & 0x0f;
    return (config_bits < 10) ? config_bits : 0;
  }

}  // namepsace config

#endif


