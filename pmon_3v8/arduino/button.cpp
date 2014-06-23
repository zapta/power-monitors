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

#include "button.h"

#include "passive_timer.h"

namespace button {
  
// Button click time high limit in milliseconds.
static uint16 kMaxClickTimeMillis = 500;

// Button long press low limit in milliseconds.
static uint16 kMinLongPressMillis = 2000;

namespace state {
  static const uint8 kIdle = 0;
  static const uint8 kPressedShort = 1;
  static const uint8 kPressedLong = 2;  
}

static uint8 current_state;
static PassiveTimer time_in_state;

static uint8 pending_event;

// Debounce time = 50ms.
static ByteDebouncer byte_debouncer(50);
static io_pins::InputPin button_pin(PORTD, 2);

static inline void enterState(uint8 new_state) {
  current_state = new_state;
  time_in_state.restart();
}

void setup() {
  enterState(state::kIdle);
  pending_event = event::kNone;
}

void loop() {
  // Read the button and update the debouncer.
  // NOTE: since we debounce the entire byte we must used unique values for true and false.
  // NOTE: the button is active low so we invert the signal.
  const uint8 new_value = button_pin.isHigh() ? 0 : 1; 
  byte_debouncer.update(new_value);
  
  const uint8 is_pressed = byte_debouncer.stableValue();
  
  switch (current_state) {
    case state::kIdle:
      if (is_pressed) {
        enterState(state::kPressedShort);
      }
      return;
      
    case state::kPressedShort: {
        const uint32 t = time_in_state.timeMillis();
        if (t >= kMinLongPressMillis) {
          pending_event = event::kLongPress;
          enterState(state::kPressedLong);
        } else if (!is_pressed) {
          if (t <= kMaxClickTimeMillis) {
             pending_event = event::kClick;
          } 
          enterState(state::kIdle);
        }
      }
      return;
      
    case state::kPressedLong:
      if (!is_pressed) {
        enterState(state::kIdle);
      }
      return;
      
    default:
      enterState(state::kIdle);
      return;
  }
}

uint8 consumeEvent() {
  const uint8 result = pending_event;
  pending_event = event::kNone;
  return result;
}

boolean isButtonPressed() {
  return current_state != state::kIdle;
}

boolean hasStableValue() {
  return byte_debouncer.hasStableValue();
}
  
}  // namespace buttons



