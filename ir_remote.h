#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include <Arduino.h>

// Определяем IR_RECEIVE_PIN перед включением библиотеки (требование IRremote 4.x)
#ifndef IR_RECEIVE_PIN
#define IR_RECEIVE_PIN 4
#endif

#include <IRremote.hpp>

#ifndef IR_ENABLE
#define IR_ENABLE 1
#endif

#ifndef IR_PRINT_RAW
#define IR_PRINT_RAW 1
#endif

// IR команды VAKIO (протокол NEC, адрес 0x0)
#define IR_CMD_OFF          0x0
#define IR_CMD_WINTER       0x1
#define IR_CMD_NIGHT        0x2
#define IR_CMD_SPEED_UP     0x5
#define IR_CMD_SPEED_DOWN   0x9
#define IR_CMD_INFLOW       0xC
#define IR_CMD_RECUPERATION 0xD
#define IR_CMD_OUTFLOW      0xE

static bool irEnabled = false;

// Callback для обработки IR команд
static void (*irCommandCallback)(uint8_t cmd) = nullptr;

static void setIrCommandCallback(void (*callback)(uint8_t)) {
  irCommandCallback = callback;
}

static void irInit(uint8_t pin) {
#if IR_ENABLE
  Serial.print("IR init on pin ");
  Serial.println(pin);
  Serial.print("IRremote lib version: ");
  Serial.print(VERSION_IRREMOTE_MAJOR);
  Serial.print(".");
  Serial.println(VERSION_IRREMOTE_MINOR);

  IrReceiver.begin(pin, ENABLE_LED_FEEDBACK);
  Serial.println("IR receiver started");
  irEnabled = true;
#else
  Serial.println("IR disabled by IR_ENABLE=0");
#endif
}

static void irUpdate() {
#if IR_ENABLE
  if (!irEnabled) {
    return;
  }

  if (IrReceiver.decode()) {
    bool isRepeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;

#if IR_PRINT_RAW
    Serial.print("IR: protocol=");
    Serial.print(getProtocolString(IrReceiver.decodedIRData.protocol));
    Serial.print(" addr=0x");
    Serial.print(IrReceiver.decodedIRData.address, HEX);
    Serial.print(" cmd=0x");
    Serial.print(IrReceiver.decodedIRData.command, HEX);
    Serial.print(" raw=0x");
    Serial.print(IrReceiver.decodedIRData.decodedRawData, HEX);
    Serial.print(" bits=");
    Serial.print(IrReceiver.decodedIRData.numberOfBits);
    Serial.print(" repeat=");
    Serial.println(isRepeat ? "yes" : "no");
#else
    Serial.print("IR raw=0x");
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);
#endif

    // Обработка команд (игнорируем повторы)
    if (!isRepeat &&
        IrReceiver.decodedIRData.protocol == NEC &&
        IrReceiver.decodedIRData.address == 0x0 &&
        irCommandCallback != nullptr) {
      irCommandCallback(IrReceiver.decodedIRData.command);
    }

    IrReceiver.resume();
  }
#endif
}

#endif // IR_REMOTE_H
