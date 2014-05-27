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
#include "analysis.h"
#include "display_messages.h"

// Manages the 1.3" 128x64 OLED display.
namespace display {

  extern void setup();
  
  // Manipulating the internal current graph buffer.
  extern void clearGraphBuffer();
  extern void appendGraphPoint(uint16 currentMilliAmps);
  
  // Render a page with the current graph and momentary and analysis session average current.
  extern void renderGraphPage(uint16 current_milli_amps, uint16 average_current_milli_amps);
  
  // Render a page with the summary part 1 of the analysis.
  extern void renderSummary1Page(uint16 current_milli_amps, uint16 average_current_milli_amps, 
      uint16 total_charge_mah, uint16 time_seconds);
  
  // Render a page with the summary part 2 of the analysis.
  extern void renderSummary2Page(
      const analysis::PrintableMilsValue& printable_voltage,
      boolean is_awake, uint32 awake_count, 
      uint16 awake_charge_mah, uint16 time_seconds);

  extern void renderTestPage(
      const analysis::PrintableMilsValue& printable_voltage,
      const analysis::PrintablePpmValue& printable_current,
      uint8 dip_switches, boolean is_button_pressed);
      
  // Display the given display message (taking control of the entire display) and ignore 
  // other rendering requests for the specified time in millis. If the function is called
  // again within the specified min display time, the new call overrides the previous one.
  // Calling with message code kNone clears min display time of the current message, if any
  // but does not modify the display. 
  extern void showMessage(uint8 display_message_code, uint16 min_display_time_millis);
  
}  // namepsace display

#endif


