// Uploads a program to the SRA-8 loader (loader.s), then works as a
// Serial Monitor <-> FPGA bridge so the uploaded program can be used.
//
// Wiring (iCEBreaker PMOD 1A, see icebreaker.pcf):
//   FPGA uart_tx (pin 4) ------------------> UNO D10
//   FPGA uart_rx (pin 2) <--- 1k ---+------- UNO D11
//                                   +-- 2k -- GND     (5 V -> 3.3 V divider)
//   GND ------------------------------------ GND
//
// Use: load the FPGA with loader.s, open the Serial Monitor at 9600 baud
// and send any character.  The FPGA needs about 6 s after configuration
// before the loader runs.
//
// Upload protocol: 0xA5, length low, length high, the program bytes.  The
// loader answers with the 8 bit sum of the program bytes and starts the
// program.

#include <SoftwareSerial.h>
#include <avr/pgmspace.h>

const byte FPGA_RX_PIN = 10;    // UNO receives here, from FPGA uart_tx
const byte FPGA_TX_PIN = 11;    // UNO transmits here, to FPGA uart_rx

// Pause after every byte.  The loader needs well under 1 ms to store a
// byte, and has no receive buffer: a byte that arrives before the previous
// one has been read replaces it.
const unsigned int BYTE_DELAY_MS = 5;

const byte START_MARK = 0xA5;

// user_echo.s:  asm/sra8asm user_echo.s -f bin -o user_echo.bin && xxd -i user_echo.bin
const byte program[] PROGMEM = {
  0x00, 0x32, 0x00, 0x48, 0x01, 0xC0, 0x20, 0x00, 0x05, 0x50, 0x00, 0x00,
  0x16, 0x90, 0x00, 0x2C, 0x06, 0xE0, 0x00, 0x00, 0x00, 0x11, 0x00, 0x60,
  0x02, 0x31, 0x10, 0x01, 0x46, 0x90, 0x00, 0x18, 0x02, 0x32, 0x20, 0x01,
  0x02, 0x53, 0x30, 0x00, 0x06, 0x90, 0x00, 0x04, 0x01, 0xA1, 0x00, 0x00,
  0x05, 0x91, 0x00, 0x10, 0x16, 0x90, 0x00, 0x2C, 0x06, 0xC0, 0x00, 0x00,
  0x01, 0x90, 0x00, 0x00, 0x06, 0xE0, 0x00, 0x00, 0x06, 0x90, 0x00, 0x2C,
  0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x66, 0x72, 0x6F, 0x6D, 0x20, 0x70,
  0x6C, 0x20, 0x31, 0x0D, 0x0A, 0x00
};
const unsigned int PROGRAM_SIZE = sizeof(program);

SoftwareSerial fpga(FPGA_RX_PIN, FPGA_TX_PIN);

bool uploaded = false;

void sendSlowly(byte b) {
  fpga.write(b);
  delay(BYTE_DELAY_MS);
}

void upload() {
  Serial.print(F("uploading "));
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
  Serial.println(F("send any character to upload the program"));
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
    fpga.write(Serial.read());  // PC -> FPGA

  if (fpga.available())
    Serial.write(fpga.read());  // FPGA -> PC
}
