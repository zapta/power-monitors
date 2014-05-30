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

#ifndef DISPLAY_MESSAGES_H
#define DISPLAY_MESSAGES_H

#include "avr_util.h"

// Defines the messages that can be shown on the display. Does not include the 
// actual data screen.
namespace display_messages {
  
namespace code {
  static const uint8 kNone = 0;
  static const uint8 kSplashScreen = 1;
  static const uint8 kLtc2943InitError = 2;
  static const uint8 kAnalysisReset = 3;
  static const uint8 kGeneralError = 4;
  static const uint8 kTestMode = 5;
}

}  // namespace display_messages

#endif


