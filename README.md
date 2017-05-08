# HD44780U-RPi-Linux-Driver
Hitachi HD44780U LCD Display Linux driver for Raspberry Pi, though it can be easily adapted for other boards, the driver itself only needs to know at which gpio pin is connected each pin of the LCD controller.
Therefore the only thing to do is hooking up the Hitachi HD44780U to your board and supply the correct gpio numbering to the module parameters exposed by the device driver.
# Writing to the LCD
```
$ echo 'Hello World!' > /dev/hitachi-lcd
```
## Module Parameters
* **modeset**: set display mode on module insertion (default = 1)
* **max_chars** : available chars in the display matrix
* **pin_d4**    : gpio pin number connected to the D4 port on the HD44780U
* **pin_d5**    : gpio pin number connected to the D5 port on the HD44780U
* **pin_d6**    : gpio pin number connected to the D6 port on the HD44780U
* **pin_d7**    : gpio pin number connected to the D7 port on the HD44780U
* **pin_rs**    : gpio pin number connected to the RS port on the HD44780U
* **pin_rw**    : gpio pin number connected to the R/W port on the HD44780U
* **pin_e**     : gpio pin number connected to the E port on the HD44780U

## In Action
![alt text](https://github.com/ichigo663/HD44780U-RPi-Linux-Driver/blob/master/hitachi-lcd.jpg "Hitachi LCD with RPi3")

### About modeset
If the LCD is not powered down when unloading and loading the module the subsequent loadings must be done with modeset=0
