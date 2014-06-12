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

#include "display.h"

#include "Arduino.h"
#include "math.h"
#include "passive_timer.h"
#include "U8glib.h"

// TODO: *** make all the literal string F() strings (flash).

namespace display {
  
// Mapping from AVR port/pin Arduino Mini Pro digital pint number.
// Requires for the U8Glib graphic lib which accept pin ids using the
// Arduino digital pin numbering.
namespace pin_numbers {
  static const uint8_t kPD6_pin =  6;  
  static const uint8_t kPD7_pin =  7; 
}

// Using hardware SPI with 2X buffer which results in 4 drawing
// passes instead of the normal 8.
//
// Relevant links: 
// http://forum.arduino.cc/index.php?topic=217290.0
// http://code.google.com/p/u8glib/wiki/device
static U8GLIB_SSD1306_128X64_2X u8g(
  U8G_PIN_NONE,            // C/S, not used.
  pin_numbers::kPD7_pin,   // D/C
  pin_numbers::kPD6_pin);  // RST
	
// Represent that display message that can override the live data
// rendering. Message codes are defined in display_message.h.
static uint8 current_display_message_code;
static uint16 current_display_message_min_time_millis;
static PassiveTimer time_in_current_display_message;

// Realtime momentrary current graph data.
static const uint8 kGraphMaxPoints = 64;
static uint8 graph_y_points[kGraphMaxPoints];
static uint8 graph_first_y_index;
static uint8 graph_active_y_count;

void clearGraphBuffer() {
  graph_first_y_index = 0;
  graph_active_y_count = 0;
}

static inline void incrementGraphIndex(uint8* index) {
  if ((++(*index)) >= kGraphMaxPoints) {
    *index = 0;
  }
}

static inline uint8 currentMilliAmpsToDisplayY(uint16 current_milli_amps) {
  // Clip top range.
  if (current_milli_amps > 2000) {
    current_milli_amps = 2000;
  }

  // Using a sub logarithmic function (k1 > 0) to reduce the gain at the lower range
  // and increase the gain at the higher end. This function is betwene linear and 
  // log().
  //
  // Formulas (depending for a given a)
  // b = int(32/(ln(2000+a)-ln(a)))
  // c = =int(ln(a)*b)
  //
  static const uint16 kA = 30;
  static const uint16 kB = 7;
  static const uint16 kC = 23;
  
  // Maps [0..2000] to [0..31]
  const int scalledValue = 0.5f + ((log(current_milli_amps + kA) * kB) - kC);
  
  // Mapss [0..2000] to [63..32] (the bottom half of the display).
  return 63 - scalledValue;
}

void appendGraphPoint(uint16 current_milli_amps) {
  // Convert current milliamps to screen y coordinate.
  const uint8 y_value = currentMilliAmpsToDisplayY(current_milli_amps);
  
  // Handle the case where the buffer is full.
  if (graph_active_y_count == kGraphMaxPoints) {
    graph_y_points[graph_first_y_index] = y_value;
    incrementGraphIndex(&graph_first_y_index);
    return;    
  }
  
  // Handle the case of a non full graph buffer.
  uint8 insertion_index = graph_first_y_index + graph_active_y_count;
  if (insertion_index >= kGraphMaxPoints) {
    insertion_index -= kGraphMaxPoints;
  }
  graph_y_points[insertion_index] = y_value;
  graph_active_y_count++;
}

void setup() {
  current_display_message_code = display_messages::code::kNone;
  current_display_message_min_time_millis = 0;
  time_in_current_display_message.restart();
  
  clearGraphBuffer();
  // B&W mode. This display does not support gray scales.
  u8g.setColorIndex(1);
}

// Returns true if we have a current display message request and it's still within it
// min time period.
static boolean isActiveDisplayMessage() {
  if (current_display_message_code == display_messages::code::kNone) {
    return false;
  } 

  if (time_in_current_display_message.timeMillis() < current_display_message_min_time_millis) {
    return true;
  }
  // Display message expired, mark as done.
  current_display_message_code = display_messages::code::kNone;
  return false;
}

// ----- Graph Page

// The picture loop function. Check u8glib documentation for restrictions. This function
// is called multiple time per onw screen draw.
static inline void drawGraphPage(uint8 drawing_stripe_index, const char* current, const char* average_current) {
  // TODO: move the numeric to string conversion from the caller to here (per stripe).
  if (drawing_stripe_index == 0) {
    u8g.setFont(u8g_font_8x13);
    u8g.drawStrP(0, 10, U8G_PSTR("I"));
    u8g.drawStr(70, 10, current);
  }
  
  if (drawing_stripe_index == 1) { 
    u8g.setFont(u8g_font_8x13);
    u8g.drawStrP(0, 25, U8G_PSTR("Iavg"));
    u8g.drawStr(70, 25, average_current);
  }

  if (drawing_stripe_index >= 2) {
    // If the buffer is empty, this is still valid.
    uint8_t last_y = graph_y_points[graph_first_y_index];
    uint8_t last_x = 0;
    uint8_t index = graph_first_y_index;
  
    // We iterate starting from the second point.
    // If the buffer has less than two points, this will have zero iterations.
    for (uint8 i = 1; i < graph_active_y_count; i++) {
      // Increment to next point
      index++;
      if (index >= kGraphMaxPoints) {
        index = 0;
      }
      
      uint8_t y = graph_y_points[index];
      uint8_t x = last_x + 2;
      u8g.drawLine(last_x, last_y, x, y);
      last_y = y;
      last_x = x;
    }
    u8g.drawLine(last_x+1, 63, last_x+1, 32);
  }
    
  if (drawing_stripe_index == 3) {
    u8g.drawLine(0, 63, 127, 63);
  }
}

void renderGraphPage(uint16 current_milli_amps, uint16 average_current_milli_amps) {
  // Active display messages have higher priority.
  if (isActiveDisplayMessage()) {
    return;
  }
  
  char bfr1[10];
  snprintf_P(bfr1, sizeof(bfr1), PSTR("%4d ma"), current_milli_amps);
  
  char bfr2[10];
  snprintf_P(bfr2, sizeof(bfr2), PSTR("%4d ma"), average_current_milli_amps);
  
  // Execute the picture loop. We track the draw stripe index so we can 
  // render on each stripe so we can skip drawing graphics object on stripes
  // they do not intersect (faster drawing).
  u8g.firstPage();   
  uint8 drawing_stripe_index = 0;
  do {
    drawGraphPage(drawing_stripe_index++, bfr1, bfr2);
  } while (u8g.nextPage());
}

// ----- Summary 1 Page

// The picture loop function. Check u8glib documentation for restrictions. This function
// is called multiple time per onw screen draw.
static inline void drawSummary1Page(uint8 drawing_stripe_index, 
    uint16 current_milli_amps, uint16 average_current_milli_amps, uint16 total_charge_mah, uint16 time_seconds) {
  u8g.setFont(u8g_font_8x13);
  
  char bfr[12];

  if (drawing_stripe_index == 0) {
    const uint8 kBaseY = 10;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("I"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%4u"), current_milli_amps);
    u8g.drawStr(65, kBaseY, bfr);
    u8g.drawStrP(103, kBaseY, U8G_PSTR("ma"));
  }
  
  if (drawing_stripe_index == 1) {
    const uint8 kBaseY = 27;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("Iavg"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%4u"), average_current_milli_amps);
    u8g.drawStr(65, kBaseY, bfr);
    u8g.drawStrP(103, kBaseY, U8G_PSTR("ma"));
  }
  
  if (drawing_stripe_index == 2) {
    const uint8 kBaseY = 44;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("Q"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%4u"), total_charge_mah);
    u8g.drawStr(65, kBaseY, bfr);
    u8g.drawStrP(103, kBaseY, U8G_PSTR("mah"));
  }

  if (drawing_stripe_index == 3) {
    const uint8 kBaseY = 61;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("T"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%6u"), time_seconds);
    u8g.drawStr(49, kBaseY, bfr);
    u8g.drawStrP(101, kBaseY, U8G_PSTR("sec"));
  }
}

void renderSummary1Page(uint16 current_milli_amps, uint16 average_current_milli_amps, 
    uint16 total_charge_mah, uint16 time_seconds) {
  // Active display messages have higher priority.
  if (isActiveDisplayMessage()) {
    return;
  }  
  // See comments for similar code in renderGraphPage().
  u8g.firstPage();   
  uint8 drawing_stripe_index = 0;
  do {
    drawSummary1Page(drawing_stripe_index++, current_milli_amps, average_current_milli_amps, total_charge_mah, time_seconds);
  } while (u8g.nextPage());      
}

// ----- Summary 2 Page

static inline void drawSummary2Page(
    uint8 drawing_stripe_index, 
    const analysis::PrintableMilsValue& printable_voltage,
    boolean is_awake, uint32 awake_count,  
    uint16 awake_charge_mah, uint16 time_seconds) {
  u8g.setFont(u8g_font_8x13);
  
  char bfr[12];

  if (drawing_stripe_index == 0) {
    const uint8 kBaseY = 10;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("V"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%u.%03u"), printable_voltage.units, printable_voltage.mils);
    u8g.drawStr(57, kBaseY, bfr);
    u8g.drawStrP(103, kBaseY, U8G_PSTR("v"));
  }
  
  if (drawing_stripe_index == 1) {
    const uint8 kBaseY = 27;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("Nwake"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%5lu"), awake_count);
    u8g.drawStr(57, kBaseY, bfr);
    if (is_awake) {
      u8g.drawStrP(103, kBaseY, U8G_PSTR("*"));
    }
  }
  
  if (drawing_stripe_index == 2) {
    const uint8 kBaseY = 44;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("Qwake"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%4u"), awake_charge_mah);
    u8g.drawStr(65, kBaseY, bfr);
    u8g.drawStrP(103, kBaseY, U8G_PSTR("mah"));
  }

  if (drawing_stripe_index == 3) {
    const uint8 kBaseY = 61;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("T"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%6u"), time_seconds);
    u8g.drawStr(49, kBaseY, bfr);
    u8g.drawStrP(101, kBaseY, U8G_PSTR("sec"));
  }         
}

void renderSummary2Page(
    const analysis::PrintableMilsValue& printable_voltage,
    boolean is_awake, uint32 awake_count, 
    uint16 awake_charge_mah, uint16 time_seconds) {
  // Active display messages have higher priority.
  if (isActiveDisplayMessage()) {
    return;
  }  
  // See comments for similar code in renderGraphPage().
  u8g.firstPage();   
  uint8 drawing_stripe_index = 0;
  do {
    drawSummary2Page(drawing_stripe_index++, printable_voltage, is_awake, awake_count,  
    awake_charge_mah, time_seconds);
  } while (u8g.nextPage());  
}


// ---------- Test page

static inline void drawTestPage(
    uint8 drawing_stripe_index, 
    const analysis::PrintableMilsValue& printable_voltage,
    const analysis::PrintablePpmValue& printable_current,
    uint16 charge_register, boolean is_button_pressed) {
  u8g.setFont(u8g_font_8x13);
  
  char bfr[12];
  
  if (drawing_stripe_index == 0) {
    const uint8 kBaseY = 10;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("V"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%u.%03u"), printable_voltage.units, printable_voltage.mils);
    u8g.drawStr(57, kBaseY, bfr);
  }
  
  if (drawing_stripe_index == 1) {
    const uint8 kBaseY = 27;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("I"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%u.%03u"), printable_current.units, printable_current.mils);
    u8g.drawStr(57, kBaseY, bfr);
  }
  
  if (drawing_stripe_index == 2) {
    const uint8 kBaseY = 44;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("R"));
    snprintf_P(bfr, sizeof(bfr), PSTR("%04X"), charge_register);
    u8g.drawStr(57, kBaseY, bfr);
  }
  
  if (drawing_stripe_index == 3) {
    const uint8 kBaseY = 61;
    u8g.drawStrP(0, kBaseY, U8G_PSTR("B"));
    u8g.drawStr(57, kBaseY, (is_button_pressed ? "*" : "_"));
  }
}

void renderTestPage(
    const analysis::PrintableMilsValue& printable_voltage,
    const analysis::PrintablePpmValue& printable_current,
    uint16 charge_register, boolean is_button_pressed) {
  // Active display messages have higher priority.
  if (isActiveDisplayMessage()) {
    return;
  }  
  // See comments for similar code in renderGraphPage().
  u8g.firstPage();   
  uint8 drawing_stripe_index = 0;
  do {
    drawTestPage(drawing_stripe_index++, printable_voltage, printable_current,
        charge_register, is_button_pressed);
  } while (u8g.nextPage());  
}
      
// ---------- Display Message Page

static void drawCurrentDisplayMessage() {
  u8g.setFont(u8g_font_8x13);
  // NOTE: drawRFrame adds about 600 bytes to the flash size compared to drawFrame.
  // May be pulling the cirlce code for the rounded corners.
  u8g.drawRFrame(0, 0, 128, 64, 5);
  
  if (current_display_message_code == display_messages::code::kSplashScreen) {
    u8g.drawStrP(22, 25, U8G_PSTR("POWER PLAY"));
    // TODO: define the version id in a common file and also print to serial output.
    u8g.drawStrP(27, 46, U8G_PSTR("Ver 0.200"));
    return;
  }
  
  if (current_display_message_code == display_messages::code::kAnalysisReset) {
    u8g.drawStrP(21, 26, U8G_PSTR("Restarting"));
    u8g.drawStrP(21, 45, U8G_PSTR("analysis..."));
    return;
  }
  
  if (current_display_message_code == display_messages::code::kLtc2943InitError) {
    u8g.drawStrP(11, 26, U8G_PSTR("Current sense"));
    u8g.drawStrP(11, 45, U8G_PSTR("comm error."));
    return;
  }
  
  if (current_display_message_code == display_messages::code::kGeneralError) {
    u8g.drawStrP(11, 26, U8G_PSTR("Error"));
    u8g.drawStrP(11, 45, U8G_PSTR("detected."));
    return;
  }
  
  if (current_display_message_code == display_messages::code::kTestMode) {
    u8g.drawStrP(27, 37, U8G_PSTR("Test Mode"));
    return;
  }
  
  // Else, unknown message. Show message code.
  u8g.drawStrP(15, 33, U8G_PSTR("Message: "));
  char bfr[12];
  snprintf_P(bfr, sizeof(bfr), PSTR("%4d"), current_display_message_code);
  u8g.drawStr(65, 33, bfr);
}

void renderCurrentDisplayMessage() {
  u8g.firstPage();   
  do {
    drawCurrentDisplayMessage();
  } while (u8g.nextPage());    
}
  
// -----
  
void showMessage(uint8 display_message_code, uint16 min_display_time_millis) {
  // Save the new display message info.
  const uint8 previous_display_message_code = current_display_message_code;
  current_display_message_code = display_message_code;
  current_display_message_min_time_millis = min_display_time_millis;
  time_in_current_display_message.restart();
  
  // Skip if no need to update the display.
  if (display_message_code == display_messages::code::kNone || display_message_code == previous_display_message_code) {
    return;  
  }
  
  // Update the display
  renderCurrentDisplayMessage();
}

}  // namepsace display

