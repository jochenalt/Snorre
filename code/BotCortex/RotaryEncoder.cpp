#include "Arduino.h"
#include "RotaryEncoder.h"
#include "BotMemory.h"
#include "utilities.h"

void RotaryEncoder::setup(ActuatorConfiguration* pActuatorConfig, RotaryEncoderConfig* pConfigData, RotaryEncoderSetupData* pSetupData)
{
	configData = pConfigData;
	setupData = pSetupData;
	actuatorConfig = pActuatorConfig;

	passedCheck= false;	
	
	if (memory.persMem.logSetup) {
		logger->print(F("   setup encoder(0x"));
		logger->print(setupData->I2CAddress,HEX);
		logger->println(")");
		logger->print(F("   "));
		configData->print();
		logger->print(F("   "));
		setupData->print();
	}
	
	if (memory.persMem.logSetup) {
		logger->print(F("   connecting to I2C 0x"));
		logger->print(i2CAddress(), HEX);
		logger->println();
		logger->print(F("   "));
	}

	sensor.begin(i2CBus());
	sensor.setI2CAddress(i2CAddress());
	//set clock wise counting
	sensor.setClockWise(isClockwise());

	// check communication		
	currentSensorAngle = sensor.angleR(U_DEG, true);

	// check communication
	communicationWorks = false;
	i2CBus()->beginTransmission(i2CAddress());
	byte error = i2CBus()->endTransmission();
	communicationWorks = (error == 0);
	logger->print(F("comcheck(0x"));
	logger->print(i2CAddress(), HEX);
	logger->print(F(") "));
	if (!communicationWorks) 
		logger->println(F("failed!"));
	else {
		logger->print(F("ok"));
	}
	currentSensorAngle = 0.0;
	if (communicationWorks) {
		currentSensorAngle = sensor.angleR(U_DEG, true);	
		logger->print(F(" angle="));
		logger->print(currentSensorAngle);
		logger->print(F(" offset="));
		logger->println(actuatorConfig->angleOffset);
	}
} 


float RotaryEncoder::getAngle() {
	float angle = currentSensorAngle;
	
	angle -= actuatorConfig->angleOffset;
	return angle;
}

void RotaryEncoder::setNullAngle(float rawAngle) {
	configData->nullAngle = rawAngle;
}

float RotaryEncoder::getAngleOffset() {
	return actuatorConfig->angleOffset;
}

float RotaryEncoder::getNullAngle() {
	return configData->nullAngle;
}

float RotaryEncoder::getLastRawSensorAngle() {
	return currentSensorAngle;
}

bool RotaryEncoder::readNewAngleFromSensor() {
	float rawAngle = sensor.angleR(U_DEG, true); // returns angle between 0..360
	float nulledRawAngle = rawAngle - getNullAngle();
	if (nulledRawAngle> 180.0)
		nulledRawAngle -= 360.0;
	if (nulledRawAngle< -180.0)
		nulledRawAngle += 360.0;

	if (sensor.endTransmissionStatus() != 0) {
		failedReadingCounter = max(failedReadingCounter, failedReadingCounter+1);
		logActuator(setupData->id);
		logger->print(failedReadingCounter);
		logger->print(F(".retry "));
		logError(F("enc comm"));
		return false;
	} else {
		failedReadingCounter = 0;
	}
	
	// apply first order low pass to filter sensor noise
	if (filterAngle) {
		uint32_t now = millis();
		float duration_s = float(now - lastSensorRead) * (1.0/1000.0);
		lastSensorRead = now;

		const float reponseTime = float(ENCODER_FILTER_RESPONSE_TIME)/1000.0;	// signal changes shorter than 2 samples are filtered out
		float complementaryFilter = reponseTime/(reponseTime + duration_s);
		float antiComplementaryFilter = 1.0 - complementaryFilter;
		currentSensorAngle = antiComplementaryFilter*nulledRawAngle + complementaryFilter*currentSensorAngle;
	}
	else {
		currentSensorAngle = nulledRawAngle;
	}

	return true;
}


bool RotaryEncoder::fetchSample(uint8_t no, float sample[], float& avr, float &variance) {
	avr = 0.;
	filterAngle = false;
	avr = 0;
	for (int check = 0;check<no;check++) {
		if (check > 0) {
			delay(10); // that's not bad, this function is called for calibration only, not during runtime
		}

		readNewAngleFromSensor(); // measure the encoder's angle
		float x = getLastRawSensorAngle();
		sample[check] = x;
		avr += x;
	}
	filterAngle = true;

	avr = avr/float(no);
	// compute average and variance, and check if values are reasonable;
	variance = 0;
	for ( int check = 0;check<no;check++) {
		float d = sample[check]-avr;
		variance += d*d;
	}
	variance = variance/no;
	return (variance <= ENCODER_CHECK_MAX_VARIANCE);
}

bool RotaryEncoder::fetchSample(float& avr, float &variance) {
	float sample[ENCODER_CHECK_NO_OF_SAMPLES];
	bool ok = fetchSample(ENCODER_CHECK_NO_OF_SAMPLES,sample,avr, variance);
	return ok;
}

float RotaryEncoder::checkEncoderVariance() {
	// collect samples of all encoders
	float value[ENCODER_CHECK_NO_OF_SAMPLES];
	float avr, variance;
	passedCheck = fetchSample(ENCODER_CHECK_NO_OF_SAMPLES,value, avr, variance);

	if (memory.persMem.logEncoder) {
		logger->print(F("encoder("));
		logActuator(setupData->id);
		logger->print(")");

		if (!passedCheck) {
			logger->print(F(" avr="));
			logger->print(avr);
	
			logger->print(F(" var="));
			logger->print(variance);
			logger->print(F(" not"));
		}
		logger->println(F(" stable."));
	}
	return variance;
}
