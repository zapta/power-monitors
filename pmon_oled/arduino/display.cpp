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
static uint16 last_current_milli_amps;

void clearGraphBuffer() {
  graph_first_y_index = 0;
  graph_active_y_count = 0;
  last_current_milli_amps = 0;
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
  
  const int scalledValue = log(current_milli_amps) * 4;
  
  // Mapss [0..2000] to [63..32]
  return 63 - scalledValue;
}

void appendCurrentGraphPoint(uint16 current_milli_amps) {
  last_current_milli_amps = current_milli_amps;
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
void draw(uint8 drawing_stripe_index, const char* current, const char* average_current) {
  if (drawing_stripe_index < 2) {
    //u8g.setFont(u8g_font_unifont);
    u8g.setFont(u8g_font_8x13);
  }
  
  if (drawing_stripe_index == 0) {
    u8g.drawStr( 0, 10, "Current");
    u8g.drawStr( 70, 10, current);
  }
  
  if (drawing_stripe_index == 1) { 
    u8g.drawStr( 0, 25, "Average");
    u8g.drawStr( 70, 25, average_current);
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

void setup() {
  clearGraphBuffer();
  // B&W mode. This display does not support gray scales.
  u8g.setColorIndex(1);
}

void updateDisplay(uint16 average_current_milli_amps) {
  // U8Glib draws the screen in horizontal section to trade CPU time for RAM efficiency.
  static char bfr1[10];
  snprintf(bfr1, sizeof(bfr1), "%4d ma", last_current_milli_amps);
    static char bfr2[10];
  snprintf(bfr2, sizeof(bfr2), "%4d ma", average_current_milli_amps);
  
  // Execute the picture loop. We track the draw stripe index so we can 
  // render on each stripe so we can skip drawing graphics object on stripes
  // they do not intersect (faster drawing).
  u8g.firstPage();   
  uint8 drawing_stripe_index = 0;
  do {
    draw(drawing_stripe_index++, bfr1, bfr2);
  } while (u8g.nextPage());
}
  
}  // namepsace display



