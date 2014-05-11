
#include "U8glib.h"

// Requires installation of the u8glib library (download zip file and then use Sketch -> Import Library...).
//
// SW SPI
// CLK = Arduino digital pin 13
// SDA = Arduino digital pin 11
// D/C = Arduino Digital pin 9
// RST = Arduino Digital pin 2
// CS  = Arduino digital pin 10 (not used)
//
// For more info: 
// http://forum.arduino.cc/index.php?topic=217290.0
// http://code.google.com/p/u8glib/wiki/device

U8GLIB_SSD1306_128X64 u8g(13, 11, 10, 9, 2);	// SW SPI Com: SCK = 13, MOSI = 11, CS = 10, A0 = 9

const uint8_t y_values[] =  {
  60, // ****
  60, // ****
  60, // ****
  60, // ****
  60, // ****
  57, // *******
  59, // *****
  60, // ****
  60, // ****
  57, // *******
  55, // *********
  50, // **************
  49, // ***************
  50, // **************
  47, // *****************
  42, // **********************
  42, // **********************
  52, // ************
  50, // **************
  47, // *****************
  50, // **************
  53, // ***********
  56, // ********
  57, // *******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  57, // *******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  58, // ******
  59, // *****
  60, // ****
  60, // ****
  60, // ****
  59, // *****
  59, // *****
  60, // ****
  60, // ****
  60, // ****
  59, // *****
  59, // *****
  59, // *****
  60, // ****
  60, // ****
  59, // *****
  59, // *****
};

const uint8_t kPointCount = sizeof(y_values)/sizeof(y_values[0]);

uint8_t offset = 0;


// The picture loop function. Check u8glib documentation for restrictions. This function
// is called multiple time per onw screen draw.
void draw(const char* value) {
  // graphic commands to redraw the complete screen should be placed here  
  u8g.setFont(u8g_font_unifont);
  //u8g.setFont(u8g_font_osb21);
  
  u8g.drawStr( 0, 20, "Current");
  u8g.drawStr( 60, 20, value);
   
  uint8_t last_y = y_values[offset];
  uint8_t last_x = 0;
  uint8_t index = offset;
  
  for (;;) {
    index++;
    if (index >= kPointCount) {
      index = 0;
    }
    if (index == offset) {
      break;
    }
    uint8_t y = y_values[index];
    uint8_t x = last_x + 2;
    u8g.drawLine(last_x, last_y, x, y);
    last_y = y;
    last_x = x;
  }
}

static const int kDebugPin = 4;

void setup(void) {
  pinMode(kDebugPin, OUTPUT);
 
 Serial.begin(115200);          
 Serial.println("Hello world!");
  
 // B&W mode. This display does not support gray scales.
 u8g.setColorIndex(1);
}

void loop(void) {
  // picture loop
  u8g.firstPage();  
  static char bfr[10];
  snprintf(bfr, sizeof(bfr), "%4d ma", (60 - y_values[offset >> 2]) << 5);
  do {
    digitalWrite(kDebugPin, HIGH);
    draw(bfr);
    digitalWrite(kDebugPin, LOW);

  } while( u8g.nextPage() );
 
  offset++;
  if (offset >= kPointCount) {
    offset = 0;
  }
  
  // rebuild the picture after some delay
  delay(50);
}

