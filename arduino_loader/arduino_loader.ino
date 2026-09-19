// Uploads a program (program.h) to the SRA-8 loader (loader.s), then works
// as a Serial Monitor <-> FPGA bridge so the uploaded program can be used.
//
// Wiring (iCEBreaker PMOD 1A, see icebreaker.pcf):
//   FPGA uart_tx (pin 4) ------------------> UNO D10
//   FPGA uart_rx (pin 2) <--- 1k ---+------- UNO D11
//                                   +-- 2k -- GND     (5 V -> 3.3 V divider)
//   GND ------------------------------------ GND
//
// Use: load the FPGA with loader.s, open the Serial Monitor at 9600 baud
// and send any character.  The FPGA needs about 6 s after configuration
// before the loader runs.  For terminal.s set the line ending of the
// Serial Monitor to "Newline" or "Carriage return".
//
// Upload protocol: 0xA5, length low, length high, the program bytes.  The
// loader answers with the 8 bit sum of the program bytes and starts the
// program.

#include <SoftwareSerial.h>
#include <avr/pgmspace.h>

const byte FPGA_RX_PIN = 10;    // UNO receives here, from FPGA uart_tx
const byte FPGA_TX_PIN = 11;    // UNO transmits here, to FPGA uart_rx

// Pause after every byte, during the upload and afterwards in the bridge.
// Neither the loader nor the programs have a receive buffer: a byte that
// arrives before the previous one has been read replaces it.  The loader
// needs well under 1 ms for a byte, terminal.s about 2 ms because it echoes.
// The pause is also when SoftwareSerial can hear the echo, it does not
// receive while it sends.
const unsigned int BYTE_DELAY_MS = 5;

const byte START_MARK = 0xA5;

// The program to upload.  Make program.h with
//   python3 arduino_loader/make_program.py terminal.s      (or user_echo.s, ...)
#include "program.h"
const unsigned int PROGRAM_SIZE = sizeof(program);

SoftwareSerial fpga(FPGA_RX_PIN, FPGA_TX_PIN);

bool uploaded = false;

void sendSlowly(byte b) {
  fpga.write(b);
  delay(BYTE_DELAY_MS);
}

void upload() {
  Serial.print(F("uploading "));
  Serial.print(PROGRAM_NAME);
  Serial.print(F(", "));
  Serial.print(PROGRAM_SIZE);
  Serial.println(F(" bytes"));

  while (fpga.available())      // drop the loader's banner
    fpga.read();

  byte sum = 0;
  sendSlowly(START_MARK);
  sendSlowly(lowByte(PROGRAM_SIZE));
  sendSlowly(highByte(PROGRAM_SIZE));
  for (unsigned int i = 0; i < PROGRAM_SIZE; i++) {
    byte b = pgm_read_byte(program + i);
    sum += b;
    sendSlowly(b);
  }

  unsigned long start = millis();
  while (!fpga.available() && millis() - start < 1000)
    ;

  if (!fpga.available()) {
    Serial.println(F("no answer from the loader, send a character to try again"));
    return;
  }
  byte answer = fpga.read();
  if (answer != sum) {
    Serial.print(F("checksum mismatch: sent "));
    Serial.print(sum, HEX);
    Serial.print(F(", loader got "));
    Serial.println(answer, HEX);
    Serial.println(F("reconfigure the FPGA to run the loader again"));
    return;
  }
  Serial.println(F("checksum ok, the program is running"));
  uploaded = true;
}

void setup() {
  Serial.begin(9600);
  fpga.begin(9600);             // must match DIV in UartTx.v / UartRx.v
  Serial.print(F("send any character to upload "));
  Serial.println(PROGRAM_NAME);
}

void loop() {
  if (!uploaded) {
    if (fpga.available())
      Serial.write(fpga.read());        // the loader's banner
    if (Serial.available()) {
      while (Serial.available()) {      // the whole line is only the trigger
        Serial.read();
        delay(5);
      }
      upload();
    }
    return;
  }

  if (Serial.available())
    sendSlowly(Serial.read());  // PC -> FPGA

  if (fpga.available())
    Serial.write(fpga.read());  // FPGA -> PC
}
