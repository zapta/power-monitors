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
#include "U8glib.h"

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
  
  printf(F("%u, %u\n"), graph_first_y_index, graph_active_y_count);

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

// The picture loop function. Check u8glib documentation for restrictions. This function
// is called multiple time per onw screen draw.
static inline void drawGraphPage(uint8 drawing_stripe_index, const char* current, const char* average_current) {
  if (drawing_stripe_index == 0) {
    u8g.setFont(u8g_font_8x13);
    u8g.drawStr(0, 10, "Current");
    u8g.drawStr(70, 10, current);
  }
  
  if (drawing_stripe_index == 1) { 
    u8g.setFont(u8g_font_8x13);
    u8g.drawStr(0, 25, "Average");
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

// The picture loop function. Check u8glib documentation for restrictions. This function
// is called multiple time per onw screen draw.
static inline void drawSummaryPage(uint8 drawing_stripe_index, 
    uint16 current_milli_amps, uint16 average_current_milli_amps, uint16 total_charge_mah, uint16 time_seconds) {
  u8g.setFont(u8g_font_8x13);
  
  char bfr[12];

  if (drawing_stripe_index == 0) {
    const uint8 kBaseY = 10;
    u8g.drawStr(0, kBaseY, "I");
    snprintf(bfr, sizeof(bfr), "%4d", current_milli_amps);
    u8g.drawStr(65, kBaseY, bfr);
    u8g.drawStr(103, kBaseY, "ma");
  }
  
  if (drawing_stripe_index == 1) {
    const uint8 kBaseY = 27;
    u8g.drawStr(0, kBaseY, "Iavg");
    snprintf(bfr, sizeof(bfr), "%4d", average_current_milli_amps);
    u8g.drawStr(65, kBaseY, bfr);
    u8g.drawStr(103, kBaseY, "ma");
  }
  
  if (drawing_stripe_index == 2) {
    const uint8 kBaseY = 44;
    u8g.drawStr(0, kBaseY, "Q");
    snprintf(bfr, sizeof(bfr), "%4d", total_charge_mah);
    u8g.drawStr(65, kBaseY, bfr);
    u8g.drawStr(103, kBaseY, "mah");
  }

  if (drawing_stripe_index == 3) {
    const uint8 kBaseY = 61;
    u8g.drawStr(0, kBaseY, "T");
    snprintf(bfr, sizeof(bfr), "%6d", time_seconds);
    u8g.drawStr(49, kBaseY, bfr);
    u8g.drawStr(101, kBaseY, "sec");
  }
}

void setup() {
  clearGraphBuffer();
  // B&W mode. This display does not support gray scales.
  u8g.setColorIndex(1);
}

void renderGraphPage(uint16 current_milli_amps, uint16 average_current_milli_amps) {
  char bfr1[10];
  snprintf(bfr1, sizeof(bfr1), "%4d ma", current_milli_amps);
  
  char bfr2[10];
  snprintf(bfr2, sizeof(bfr2), "%4d ma", average_current_milli_amps);
  
  // Execute the picture loop. We track the draw stripe index so we can 
  // render on each stripe so we can skip drawing graphics object on stripes
  // they do not intersect (faster drawing).
  u8g.firstPage();   
  uint8 drawing_stripe_index = 0;
  do {
    drawGraphPage(drawing_stripe_index++, bfr1, bfr2);
  } while (u8g.nextPage());
}

void renderSummaryPage(uint16 current_milli_amps, uint16 average_current_milli_amps, 
    uint16 total_charge_mah, uint16 time_seconds) {
  // See comments for similar code in renderGraphPage().
  u8g.firstPage();   
  uint8 drawing_stripe_index = 0;
  do {
    drawSummaryPage(drawing_stripe_index++, current_milli_amps, average_current_milli_amps, total_charge_mah, time_seconds);
  } while (u8g.nextPage());      
}
  
}  // namepsace display



