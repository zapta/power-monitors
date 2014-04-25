TODO list for The Arduino PMON UNO

**SCHEMATIC**
* Add a switch or jumper to power the device from the usb.
* Change to a USB based AVR (e.g. http://www.pololu.com/product/3101)
* Add display
* Add two device support (pmon duo)
* Change dip switches to a rotary switch?

**LAYOUT**

** Enclosure
* Design engraving.
* Made the plate same size as the PCB?
* Add vent holes for the LDO?

**SOFTWARE**
* Detect gracefully non responding ltc2943 (e.g. when charger is not connected). Currently
  it get hangs in the i2c::readByte* methods.

**MISCELLANEOUS**
* Add BOM document.




