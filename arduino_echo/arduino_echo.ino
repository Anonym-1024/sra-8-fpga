// Serial Monitor <-> SRA-8 UART port bridge for the Arduino UNO.
//
// Characters typed into the Serial Monitor are sent to the FPGA, and
// everything the FPGA sends is printed.  With echo.s running on the CPU,
// whatever you type comes back.
//
// Wiring (iCEBreaker PMOD 1A, see icebreaker.pcf):
//   FPGA uart_tx (pin 4) ------------------> UNO D10
//   FPGA uart_rx (pin 2) <--- 1k ---+------- UNO D11
//                                   +-- 2k -- GND     (5 V -> 3.3 V divider)
//   GND ------------------------------------ GND
//
// Serial Monitor: 9600 baud, "No line ending" to send only what you type.

#include <SoftwareSerial.h>

const byte FPGA_RX_PIN = 10;    // UNO receives here, from FPGA uart_tx
const byte FPGA_TX_PIN = 11;    // UNO transmits here, to FPGA uart_rx

SoftwareSerial fpga(FPGA_RX_PIN, FPGA_TX_PIN);

void setup() {
  Serial.begin(9600);
  fpga.begin(9600);             // must match DIV in UartTx.v / UartRx.v
}

void loop() {
  if (Serial.available())
    fpga.write(Serial.read());  // PC -> FPGA

  if (fpga.available())
    Serial.write(fpga.read());  // FPGA -> PC
}
