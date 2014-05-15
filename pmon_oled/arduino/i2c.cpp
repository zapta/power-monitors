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

#include "i2c.h"

#include <compat/twi.h>

#if F_CPU != 16000000
#error "Tested with 16Mhz only."
#endif

namespace i2c {
  
// I2C clock frequency. LTC2943 can go up to 400KHz.
static const uint32 kSclFrequency = 400000L;

// Wait until TWCR & H(bit_index) is set.
inline static void waitForTwcrBitHighWithTimeout(uint8 bit_index) {
  const uint8 mask = H(bit_index); 
  while (!(TWCR & mask)) {
  }
}

void setup()
{
  TWSR = 0; 
  // Should be >= 10 to have stable operation.  For 16Mhz cpu and 400k I2C clock we
  // get here 12 which is fine.
  TWBR = ((F_CPU / kSclFrequency) - 16) / 2; 
}

// Returns true if ok.
bool start(uint8 addr_with_op)
{
  // send START condition
  TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);

  waitForTwcrBitHighWithTimeout(TWINT);

  // Verify status.
  {
    const uint8 status = TW_STATUS & 0xF8;
    if ( (status != TW_START) && (status != TW_REP_START)) {
      return false;
    }
  }

  // Send device address
  TWDR = addr_with_op;
  TWCR = (1<<TWINT) | (1<<TWEN);

  // Wait until done.
  waitForTwcrBitHighWithTimeout(TWINT);

  // Check status.
  const uint8 status = TW_STATUS & 0xF8;
  const bool is_ok = ((status == TW_MT_SLA_ACK) || (status == TW_MR_SLA_ACK));
  return is_ok;
}

// Returns true iff ok.
void stop() {
  // Send stop condition.
  TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWSTO);	
  // Wait until done.
  waitForTwcrBitHighWithTimeout(TWSTO);
}

// Returns true if ok.
bool writeByte(uint8 data ) {	
  TWDR = data;
  TWCR = (1<<TWINT) | (1<<TWEN);

  // Wait until done.
  waitForTwcrBitHighWithTimeout(TWINT);

  // Check status.
  const uint8 twst = TW_STATUS & 0xF8;
  return (twst == TW_MT_DATA_ACK);
}


// Return true iff ok
bool readByteWithAck(uint8* result) {
  TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWEA);
  // Wait done.
  waitForTwcrBitHighWithTimeout(TWINT);
  
  *result = TWDR;
  return true; 
}


bool readByteWithNak(uint8* result) {
  TWCR = (1<<TWINT) | (1<<TWEN);
  // Wait done.	
  waitForTwcrBitHighWithTimeout(TWINT);
  *result = TWDR;
  return true; 
}

}  // namespace i2c
