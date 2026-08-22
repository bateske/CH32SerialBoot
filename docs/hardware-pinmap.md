# Board pin map (CH32X035G8U6, QFN28)

Derived from `Netlist_Schematic1_2026-08-21.tel` and cross-checked against the
CH32X035 datasheet pin table. That table lists seven packages side by side;
**QFN28 is the third numeric column** — a detail worth remembering, because
reading the wrong column makes PC16/PC17 look like pins 7/8 instead of 26/27.

Every row below was confirmed by matching the netlist's QFN28 pin numbers to the
datasheet's QFN28 column independently.

| QFN28 pin | Port  | Net          | Function            |
|-----------|-------|--------------|---------------------|
| 1         | PC15  | `BTN_RIGHT`  | D-pad right         |
| 7         | —     | `UART_TX`    | header H1.4         |
| 8         | —     | `UART_RX`    | header H1.3         |
| 9         | —     | `LCD_CS`     | ST7735 chip select  |
| 10        | —     | `CLK`        | SPI SCK (LCD + SD)  |
| 11        | —     | `MISO`       | SPI (SD)            |
| 12        | PA7   | `MOSI`       | SPI (LCD + SD)      |
| 13        | —     | `LCD_DC`     | ST7735 data/command |
| 14        | PB3   | `BTN_LEFT`   | D-pad left          |
| 15        | PB4   | `BTN_UP`     | D-pad up            |
| 16        | PB1   | `BTN_A`      | A button            |
| 17        | PB6   | `BTN_B`      | B button            |
| 18        | PB7   | `BTN_SELECT` | Select              |
| 19        | PB8   | `BTN_START`  | Start               |
| 20        | PB9   | `LED`        | status LED          |
| 21        | PB10  | `BUZZER`     | piezo               |
| 22        | —     | `SD_CS`      | microSD chip select |
| 23        | —     | `LCD_RST`    | ST7735 reset        |
| 26        | PC16  | `UDM`        | USB D-              |
| 27        | PC17  | `UDP`        | USB D+              |
| 28        | PC14  | `BTN_DOWN`   | D-pad down          |

Rows still marked `—` need their port assignment resolved from the datasheet
before the M5 variant file is written; the net names and pin numbers are certain,
only the port letter/number is outstanding.

## Status LED

`PB9 -> R9 -> LED2 -> GND`, so the LED is **active HIGH**. At reset PB9 is a
floating input and the LED is dark, which is useful: anything lighting it means
code is running and has deliberately configured the pin.

## BOOT button

`SW9` bridges `UDP` (PC17, pin 27) to 3V3 through `R10` (4.7 kΩ). The part
samples USB D+ at reset; finding it pulled high selects the factory ISP. So BOOT
must be **held across a reset**, not merely pressed. See `recovery.md`.

## No external reset pin

Option byte `RST_MOD = 0b11` disables the reset alternate function, freeing PB7
for `BTN_SELECT` as the schematic intends. The only ways to reset this part are
the power switch and `NVIC_SystemReset()` — which is why the software
boot-request path has to be reliable.
