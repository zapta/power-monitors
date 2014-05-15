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

#ifndef BUTTONS_H
#define BUTTONS_H

#include "avr_util.h"
#include "byte_debouncer.h"
#include "io_pins.h"

// Provides debounced reading of the buttons.
namespace button {
  
namespace event {
  // None is guaranteed to be zero (false).
  const uint8 kNone = 0;
  const uint8 kClick = 1;
  const uint8 kLongPress = 2;  
}

// Called from main setup.
extern void setup();

// Called from main loop.
extern void loop();
 
// Consumes and returns the next button event (one of event:: values).
extern uint8 consumeEvent();

}  // namepsace buttons

#endif


