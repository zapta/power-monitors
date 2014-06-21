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

#include "avr_util.h"

void printf(const __FlashStringHelper *format, ...)
{
  // Assuming single thread so a static buffer should be OK.
  static char buf[80];
  
  // Format the string.
  va_list ap;
  va_start(ap, format);
  vsnprintf_P(buf, sizeof(buf), (const char *)format, ap); 
  va_end(ap);
  
  // Write the string, converting LF to CR/LF.
  char* p = buf;
  for (;;) {
    const char c = *p++;
    if (c == 0) {
      break;
    }
    if (c == '\n') {
      Serial.print('\r');
    }
    Serial.print(c);  
  }
}




