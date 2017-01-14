#include <Arduino.h>
#include <i2c_t3.h>
#include "watchdog.h"
#include "ams_as5048b.h"
#include "PatternBlinker.h"
#include <I2CPortScanner.h>
#include "AccelStepper.h"
#include <pins.h>
#include "config.h"
#include "hostCommunication.h"
#include "Controller.h"
#include "BotMemory.h"
#include "AMS_AS5048B.h"
#include "core.h"
#include "LightsController.h"
#include "Printer.h"

// global variables declared in pins.h
HardwareSerial* cmdSerial = &Serial5;
HardwareSerial* logger = &Serial4;
HardwareSerial* servoComm = &Serial1;
HardwareSerial* printerComm = &Serial6;

i2c_t3* Wires[2] = { &Wire, &Wire1 };

static uint8_t IdlePattern[2] = { 0b10000000, 0b00000000, };				// boring
static uint8_t DefaultPattern[3] = { 0b11001000, 0b00001100, 0b10000000 };	// nice!
static uint8_t LEDOnPattern[1] = { 0b11111111 };
static uint8_t LEDOffPattern[1] = { 0b00000000 };
PatternBlinker ledBlinker(LED_PIN, 100);

void setLED(bool onOff) {
	// LEDs blinks a nice pattern during normal operations
	if (onOff)
		ledBlinker.set(LEDOnPattern,sizeof(LEDOnPattern));
	else
		ledBlinker.set(LEDOffPattern,sizeof(LEDOffPattern));
}

void setLEDPattern() {
	if (controller.isEnabled())
		ledBlinker.set(DefaultPattern,sizeof(DefaultPattern));
	else
		ledBlinker.set(IdlePattern,sizeof(IdlePattern));
}


void checkOrResetI2CBus(int ic2no) {

	if (Wires[ic2no]->status() != I2C_WAITING) {
		logger->println();
		logger->print(F("I2C_WAITING("));
		logger->print(ic2no);
		logger->print(F(")reset."));

		Wires[0]->resetBus();
		Wires[0]->begin();
		Wires[1]->resetBus();
		Wires[1]->begin();

		logger->print(F(" stat="));
		logger->println(Wires[ic2no]->status());
	}
}


TimePassedBy 	motorKnobTimer;		// used for measuring sample rate of motor knob
TimePassedBy 	encoderTimer;		// timer for encoder measurements

void logPinAssignment() {
	logger->println("--- pin assignment");
	logger->print("knob                = ");
	logger->println(MOTOR_KNOB_PIN);
	logger->print("Power Stepper       = ");
	logger->println(POWER_SUPPLY_STEPPER_PIN);
	logger->print("Power servo         = ");
	logger->println(POWER_SUPPLY_SERVO_PIN);
	logger->print("LED                 = ");
	logger->println(LED_PIN);

	logger->print("Cmd     RX,TX       = (");
	logger->print(SERIAL_CMD_RX);
	logger->print(",");
	logger->print(SERIAL_CMD_RX);
	logger->println(")");
	logger->print("Logger  RX,TX       = (");
	logger->print(SERIAL_LOG_RX);
	logger->print(",");
	logger->print(SERIAL_LOG_RX);
	logger->println(")");
	logger->print("Herkulex RX,TX      = (");
	logger->print(HERKULEX_RX);
	logger->print(",");
	logger->print(HERKULEX_TX);
	logger->println(")");

	logger->print("Sensor0  SCL,SDA    = (");
	logger->print(SENSOR0_SCL);
	logger->print(",");
	logger->print(SENSOR0_SDA);
	logger->println(")");

	logger->print("Sensor1  SCL,SDA    = (");
	logger->print(SENSOR1_SCL);
	logger->print(",");
	logger->print(SENSOR1_SDA);
	logger->println(")");

	logger->print("Wrist En,Dir,CLK    = (");
	logger->print(WRIST_EN_PIN);
	logger->print(",");
	logger->print(WRIST_DIR_PIN);
	logger->print(",");
	logger->print(WRIST_CLK_PIN);
	logger->print(") M=(");
	logger->print(stepperSetup[WRIST].M1Pin);
	logger->print(",");
	logger->print(stepperSetup[WRIST].M2Pin);
	logger->print(",");
	logger->print(stepperSetup[WRIST].M3Pin);
	logger->println(")");

	logger->print("Forearm En,Dir,CLK  = (");
	logger->print(FOREARM_EN_PIN);
	logger->print(",");
	logger->print(FOREARM_DIR_PIN);
	logger->print(",");
	logger->print(FOREARM_CLK_PIN);
	logger->println(")");

	logger->print("Elbow En,Dir,CLK    = (");
	logger->print(ELBOW_EN_PIN);
	logger->print(",");
	logger->print(ELBOW_DIR_PIN);
	logger->print(",");
	logger->print(ELBOW_CLK_PIN);
	logger->println(")");

	logger->print("Upperarm En,Dir,CLK = (");
	logger->print(UPPERARM_EN_PIN);
	logger->print(",");
	logger->print(UPPERARM_DIR_PIN);
	logger->print(",");
	logger->print(UPPERARM_CLK_PIN);
	logger->println(")");

	logger->print("Hip En,Dir,CLK      = (");
	logger->print(HIP_EN_PIN);
	logger->print(",");
	logger->print(HIP_DIR_PIN);
	logger->print(",");
	logger->print(HIP_CLK_PIN);
	logger->println(")");
}

// the setup routine runs once when you press reset:
void setup() {


	// until blinking led is initialized switch on the LED.
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, HIGH); // switch LED on during setup

	// the following is necessary to start the sensors properly
	pinMode(PIN_SDA0, OUTPUT);
	digitalWrite(PIN_SDA0, HIGH);
	pinMode(PIN_SCL0, OUTPUT);
	digitalWrite(PIN_SCL0, HIGH);
	pinMode(PIN_SDA1, OUTPUT);
	digitalWrite(PIN_SDA1, HIGH);
	pinMode(PIN_SCL1, OUTPUT);
	digitalWrite(PIN_SCL1, HIGH);

	// disable power supply and enable/disable switch of all motors, especially of steppers
	// to avoid ticks during powering on.
	pinMode(POWER_SUPPLY_SERVO_PIN, OUTPUT);
	digitalWrite(POWER_SUPPLY_SERVO_PIN, LOW);
	pinMode(POWER_SUPPLY_STEPPER_PIN, OUTPUT);
	digitalWrite(POWER_SUPPLY_STEPPER_PIN, LOW);

	// Cant use controller.disable, since this requires a completed setup
	for (int i = 0;i<MAX_STEPPERS;i++) {
		pinMode(stepperSetup[i].enablePIN,OUTPUT);
		digitalWrite(stepperSetup[i].enablePIN, LOW);
	}

	// establish serial output and say hello
	cmdSerial->begin(CORTEX_COMMAND_BAUD_RATE);
	cmdSerial->println("WALTER's Cortex");

	// establish logging output
	logger->begin(CORTEX_LOGGER_BAUD_RATE);
	logger->println("--- logging ---");
	logPinAssignment();

	// led controller
	lights.setup();

	// initialize
	printer.setup();
	hostComm.setup();
	memory.setup();
	controller.setup();


	setWatchdogTimeout(2000);

	//done, start blinking
	ledBlinker.set(DefaultPattern, sizeof(DefaultPattern));

	// ready for input
	cmdSerial->print(F(">"));
}


void loop() {
	watchdogReset();
	uint32_t now = millis();
	ledBlinker.loop(now);    // blink
	hostComm.loop(now);
	memory.loop(now);
	controller.loop(millis());
	lights.loop(now);

	if (controller.isSetup()) {
		checkOrResetI2CBus(0);
		checkOrResetI2CBus(1);
	}
}
