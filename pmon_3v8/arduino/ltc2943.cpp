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

#include "ltc2943.h"

#include "i2c.h"

namespace ltc2943 {
  
// The fixed address of all TLC2943 IC's. Per the datasheet, it is hard coded and 
// cannot be configured.
static const uint8 kLtc2943BaseAddress = 0x64;

// LTC2943 8 byte registers.
namespace regs8 {
  const uint8 kStatus = 0x00;
  const uint8 kControl = 0x01;
}

// LTC2943 16 byte registers.
namespace regs16 {
  const uint8 kAccumCharge = 0x02;
  const uint8 kVoltage = 0x08;
  const uint8 kCurrent = 0x0e;
  const uint8 ktemperature = 0x14;
}

// Control register fields.
static const uint8 kLtc2943AutomaticMode  = 0xc0;
static const uint8 kLtc2943PrescalarX1    = 0x00;
static const uint8 kLtc2943DisableAlccPin = 0x00;

// I2C start or repeated start for the LTC2943 read.
static inline bool startI2CRead() {
  return i2c::start((kLtc2943BaseAddress << 1) | i2c::direction::READ)  ;
}

// I2C start or repeated start for the LTC2943 write.
// Returns true if ok.
static inline bool startI2CWrite() {
  return i2c::start((kLtc2943BaseAddress << 1) | i2c::direction::WRITE)  ;
}

// Write a value to a LTC2943 8 bit register. Returns true if ok.
static bool writeReg8(uint8 reg,  uint8 value) {
  // Using boolean short circuit.
  const bool is_ok = startI2CWrite()    
      && i2c::writeByte(reg)                      
      && i2c::writeByte(value);                     
  i2c::stop(); 
  return is_ok; 
}

// Write a value to a LTC2943 16 bit register (a pair of two 8 bit registers, MSB first). 
// Returns true if ok.
static bool writeReg16(uint8 base_reg,  uint16 value) {
  // Using boolean short circuit.
  const bool is_ok = startI2CWrite()
      && i2c::writeByte(base_reg)                      
      && i2c::writeByte(highByte(value))  
      && i2c::writeByte(lowByte(value));                       
  i2c::stop(); 
  return is_ok;  
}

// Read a LTC2943 8 bit register. Returns true if ok.
static bool readReg8(uint8 reg, uint8* value) {
  // Using boolean short circuit.
  const bool is_ok = startI2CWrite()
      && i2c::writeByte(reg)  
      && startI2CRead()
      && i2c::readByteWithNak(value);
  i2c::stop();
  return is_ok;
}

// Read a LTC2943 16 bit register (a pair of two 8 bit registers, MSB first). 
// Returns true if ok.
static bool readReg16(uint8 reg, uint16* value) {
  uint8 msb;
  uint8 lsb;
  
  // Using boolean short circuit.
  const bool is_ok =  startI2CWrite()  
      && i2c::writeByte(reg)  
      && startI2CRead()
      && i2c::readByteWithAck(&msb)
      && i2c::readByteWithNak(&lsb);
  i2c::stop();
  
  // TODO: write directly to the msb, lsb bytes of value.
  // For now, in case of an error, we return undefined value.
  if (is_ok) {
    *value = word(msb, lsb);
  }
  
  return is_ok;
}

void setup() {
  i2c::setup();
}

// Returns true if ok.
bool init() {
  const uint8 control_mode = kLtc2943AutomaticMode | kLtc2943PrescalarX1 | kLtc2943DisableAlccPin ;              
  return writeReg8(regs8::kControl, control_mode);
}

// Return true if ok.
boolean readAccumCharge(uint16* value) {
  return readReg16(regs16::kAccumCharge, value);
}

// Return true if ok.
boolean readVoltage(uint16* raw_register_value, uint16* voltage_mv) {
  if (!readReg16(regs16::kVoltage, raw_register_value)) {
    return false;
  }
  // NOTE: using a top value of 0x10000 instead of 0xffff as specified in the datasheet.
  // The affect on the accuracy is insignificant.  Adding the 0x8000 to round to the closest
  // mv.
  *voltage_mv = ((((uint32)23600) * (*raw_register_value)) + 0x8000) >> 16;
  return true;
}
  
}  // namespace ltc2943



