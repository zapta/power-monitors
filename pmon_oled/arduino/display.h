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

#ifndef DISPLAY_H
#define DISPLAY_H

#include "avr_util.h"

// Manages the 1.3" 128x64 OLED display.
namespace display {

  extern void setup();
  extern void clearGraphBuffer();
  extern void appendCurrentGraphPoint(uint16 currentMilliAmps);
  extern void updateDisplay(uint16 average_current_milli_amps);
  
}  // namepsace display

#endif


