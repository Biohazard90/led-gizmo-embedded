#include <ArduinoBLE.h>

#include <Adafruit_NeoPixel.h>

#include <Wire.h>
#include <extEEPROM.h>

#include "ColorUtilities.h"

#define PIN 2
#define NUM_LEDS 5
#define NUM_AUDIO_POINTS 5
#define ANIMATION_DELAY int(1000/30) // FPS
#define BLE_DELAY 0.25f
#define BLE_DELAY_VISUALIZER 0.025f
#define EEP_ROM_PAGE_SIZE 32

Adafruit_NeoPixel pixels(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);

// Persisted data
int selectedEffect = 0;

uint8_t blinkSettings[] = {
	0xFF, 0, 0, // Color
	10, // Speed
	0, // Fade in
	1, // Fade out
	0, // Rainbow cycle
	10, // Rainbow cycle speed
};
uint8_t blinkSettingsDefaults[sizeof blinkSettings];

uint8_t waveSettings[] = {
	0xFF, 0, 0, // Color 1
	0, 0xFF, 0, // Color 2
	0, 0, 0xFF, // Color 3
	1, // Number colors

	10, // Speed
	2, // Width
	0, // Rainbow cycle
	10, // Rainbow cycle speed
};
uint8_t waveSettingsDefaults[sizeof waveSettings];

uint8_t colorWheelSettings[] = {
	255, // Brightness
	10, // Speed
	10, // Width
};
uint8_t colorWheelSettingsDefaults[sizeof colorWheelSettings];

uint8_t visualizerSettings[] = {
	255, // Brightness
	255, // Decay
};
uint8_t visualizerSettingsDefaults[sizeof visualizerSettings];

uint8_t visorSettings[] = {
	0xFF, 0, 0, // Color 1
	0, 0xFF, 0, // Color 2
	1, // Number colors
	10, // Speed
	0, // Fade in
	1, // Fade out
	0, // Rainbow cycle
	10, // Rainbow cycle speed
};
uint8_t visorSettingsDefaults[sizeof visorSettings];

uint8_t policeSettings[] = {
	0, 0, 0xFF, // Color 1
	0xFF, 0, 0, // Color 2
	10, // Speed
};
uint8_t policeSettingsDefaults[sizeof policeSettings];

uint8_t christmasSettings[] = {
	0xFF, 0xFF, 0xFF, // Color 1
	0xFF, 0, 0, // Color 2
	0, 0xFF, 0, // Color 3
	10, // Speed
};
uint8_t christmasSettingsDefaults[sizeof christmasSettings];

// Temp data
float audioData[NUM_AUDIO_POINTS] = { 0.0f };
bool audioDecay[NUM_AUDIO_POINTS] = { false };

uint8_t functionCallState[] = {
	0, // Reset call
	0, // Reset FX index
};

// GRB format
uint8_t *buffer;

// BLE
BLEService ledService(PROGMEM "e8942ca1-eef7-4a95-afeb-e3d07e8af52e");
BLEIntCharacteristic effectTypeCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa00", BLERead | BLEWrite);
BLECharacteristic blinkSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa01", BLERead | BLEWrite, sizeof blinkSettings);
BLECharacteristic waveSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa02", BLERead | BLEWrite, sizeof waveSettings);
BLECharacteristic colorWheelSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa03", BLERead | BLEWrite, sizeof colorWheelSettings);
BLECharacteristic visualizerSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa04", BLERead | BLEWrite, sizeof visualizerSettings);
BLECharacteristic visorSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa05", BLERead | BLEWrite, sizeof visorSettings);
BLECharacteristic policeSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa06", BLERead | BLEWrite, sizeof policeSettings);
BLECharacteristic christmasSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa07", BLERead | BLEWrite, sizeof christmasSettings);

// Upstream BLE
BLECharacteristic audioDataCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-20cf850bfa00", BLEWrite, sizeof audioData);
BLECharacteristic fnCallCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-20cf850bfa01", BLEWrite, sizeof functionCallState);

// EEP
extEEPROM eep(kbits_256, 1, EEP_ROM_PAGE_SIZE);
bool eepReady = false;

#define EEP_INIT 0
#define EEP_LOAD 1
#define EEP_TEST 0
#define EEP_SAVE_CHANGES 1
#define VALIDATE_BLE_BUFFERS 1

int lastTime = 0;
int animationDelayCompensation = 0;
float frameTime = 0.0f;
float bleUpdateTimer = 0;
float bleCurrentUpdateDelay = BLE_DELAY;

float connectionEffectTimer = 0.0f;

BLEDevice central;

void setup() {
	Serial.begin(115200);
	//Serial.setTimeout(50);

	copySmall(blinkSettingsDefaults, blinkSettings, sizeof blinkSettingsDefaults);
	copySmall(waveSettingsDefaults, waveSettings, sizeof waveSettingsDefaults);
	copySmall(colorWheelSettingsDefaults, colorWheelSettings, sizeof colorWheelSettingsDefaults);
	copySmall(visualizerSettingsDefaults, visualizerSettings, sizeof visualizerSettingsDefaults);
	copySmall(visorSettingsDefaults, visorSettings, sizeof visorSettingsDefaults);
	copySmall(policeSettingsDefaults, policeSettings, sizeof policeSettingsDefaults);
	copySmall(christmasSettingsDefaults, christmasSettings, sizeof christmasSettingsDefaults);

	// LED init
	pixels.begin();
	buffer = pixels.getPixels();

	// EEP init
	if (eep.begin(eep.twiClock400kHz) == 0) {
		eepReady = true;

#if EEP_INIT == 1
		eep.write(0, selectedEffect);
		eep.write(EEP_ROM_PAGE_SIZE, (byte*)&blinkSettings, sizeof blinkSettings);
		eep.write(EEP_ROM_PAGE_SIZE * 2, (byte*)&waveSettings, sizeof waveSettings);
		eep.write(EEP_ROM_PAGE_SIZE * 3, (byte*)&colorWheelSettings, sizeof colorWheelSettings);
		eep.write(EEP_ROM_PAGE_SIZE * 4, (byte*)&visualizerSettings, sizeof visualizerSettings);
		eep.write(EEP_ROM_PAGE_SIZE * 5, (byte*)&visorSettings, sizeof visorSettings);
		eep.write(EEP_ROM_PAGE_SIZE * 6, (byte*)&policeSettings, sizeof policeSettings);
		eep.write(EEP_ROM_PAGE_SIZE * 7, (byte*)&christmasSettings, sizeof christmasSettings);
#endif

#if EEP_LOAD == 1
		eep.read(0, (byte*)&selectedEffect, 1);
		eep.read(EEP_ROM_PAGE_SIZE, (byte*)&blinkSettings, sizeof blinkSettings);
		eep.read(EEP_ROM_PAGE_SIZE * 2, (byte*)&waveSettings, sizeof waveSettings);
		eep.read(EEP_ROM_PAGE_SIZE * 3, (byte*)&colorWheelSettings, sizeof colorWheelSettings);
		eep.read(EEP_ROM_PAGE_SIZE * 4, (byte*)&visualizerSettings, sizeof visualizerSettings);
		eep.read(EEP_ROM_PAGE_SIZE * 5, (byte*)&visorSettings, sizeof visorSettings);
		eep.read(EEP_ROM_PAGE_SIZE * 6, (byte*)&policeSettings, sizeof policeSettings);
		eep.read(EEP_ROM_PAGE_SIZE * 7, (byte*)&christmasSettings, sizeof christmasSettings);

		bleCurrentUpdateDelay = (selectedEffect == 3) ? BLE_DELAY_VISUALIZER : BLE_DELAY;
#endif

#if EEP_TEST == 1
		Serial.println("eep success");
		if (eep.write(0, (byte*)&selectedEffect, 1) != 0) {
			Serial.println("eep write err");
		}

		byte readTest = 255;
		if (eep.read(0, &readTest, 1) == 0) {
			Serial.println(String("eep read success: ") + String(readTest));
		}
		else {
			Serial.println("eep read err");
		}
#endif
	}
	//else {
	//	Serial.println("eep err");
	//}

	// BLE init
	//BLE.setConnectionInterval(0x0001, 0x0001);
	BLE.begin();
	BLE.setLocalName(PROGMEM "Doggy Light");

	// Service setup
	ledService.addCharacteristic(effectTypeCharacteristic);
	ledService.addCharacteristic(blinkSettingsCharacteristic);
	ledService.addCharacteristic(waveSettingsCharacteristic);
	ledService.addCharacteristic(colorWheelSettingsCharacteristic);
	ledService.addCharacteristic(visualizerSettingsCharacteristic);
	ledService.addCharacteristic(visorSettingsCharacteristic);
	ledService.addCharacteristic(policeSettingsCharacteristic);
	ledService.addCharacteristic(christmasSettingsCharacteristic);
	ledService.addCharacteristic(audioDataCharacteristic);
	ledService.addCharacteristic(fnCallCharacteristic);
	BLE.addService(ledService);

	// Characteristics init
	effectTypeCharacteristic.writeValue(selectedEffect);
	blinkSettingsCharacteristic.writeValue(blinkSettings, sizeof blinkSettings);
	waveSettingsCharacteristic.writeValue(waveSettings, sizeof waveSettings);
	colorWheelSettingsCharacteristic.writeValue(colorWheelSettings, sizeof colorWheelSettings);
	visualizerSettingsCharacteristic.writeValue(visualizerSettings, sizeof visualizerSettings);
	visorSettingsCharacteristic.writeValue(visorSettings, sizeof visorSettings);
	policeSettingsCharacteristic.writeValue(policeSettings, sizeof policeSettings);
	christmasSettingsCharacteristic.writeValue(christmasSettings, sizeof christmasSettings);
	fnCallCharacteristic.writeValue(functionCallState, sizeof functionCallState);

	// Characteristics callbacks
	effectTypeCharacteristic.setEventHandler(BLEWritten, EffectTypeChanged);
	blinkSettingsCharacteristic.setEventHandler(BLEWritten, BlinkSettingsChanged);
	waveSettingsCharacteristic.setEventHandler(BLEWritten, WaveSettingsChanged);
	colorWheelSettingsCharacteristic.setEventHandler(BLEWritten, ColorWheelSettingsChanged);
	visualizerSettingsCharacteristic.setEventHandler(BLEWritten, VisualizerSettingsChanged);
	visorSettingsCharacteristic.setEventHandler(BLEWritten, VisorSettingsChanged);
	policeSettingsCharacteristic.setEventHandler(BLEWritten, PoliceSettingsChanged);
	christmasSettingsCharacteristic.setEventHandler(BLEWritten, ChristmasSettingsChanged);
	audioDataCharacteristic.setEventHandler(BLEWritten, AudioDataChanged);
	fnCallCharacteristic.setEventHandler(BLEWritten, FnCallChanged);

	// Advertise
	BLE.setAdvertisedService(ledService);
	BLE.setAdvertisingInterval(320); // 160 == 100ms
	BLE.advertise();

	BLE.setEventHandler(BLEConnected, blePeripheralConnectedHandler);
	BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectedHandler);
}

void loop() {
	int currentTime = millis();
	frameTime = (currentTime - lastTime) / 1000.0f;
	int animationDelay = lastTime = currentTime;

	if (frameTime >= 1.0f) {
		frameTime = 0.999f;
	}

	Animate();

	UpdateBLE();

	animationDelay = ANIMATION_DELAY - (millis() - animationDelay);
	if (animationDelay < 0) {
		animationDelay = 0;
	}
	else if (animationDelay > ANIMATION_DELAY) {
		animationDelay = ANIMATION_DELAY;
	}
	delay(animationDelay);
}

void blePeripheralConnectedHandler(BLEDevice device) {
	//Serial.print("Connected event, device: ");
	//Serial.println(device.address());
}

void blePeripheralDisconnectedHandler(BLEDevice device) {
	//Serial.print("Disconnected event, device: ");
	//Serial.println(device.address());
}

void copySmall(uint8_t *dst, uint8_t *src, int size) {
	--size;
	for (; size >= 0; --size) {
		dst[size] = src[size];
	}
}

void EffectTypeChanged(BLEDevice device, BLECharacteristic characteristic) {
	selectedEffect = *(const int*)characteristic.value();

#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(0, selectedEffect);
	}
#endif

	bleCurrentUpdateDelay = (selectedEffect == 3) ? BLE_DELAY_VISUALIZER : BLE_DELAY;
}

void EffectSettingsChanged(uint8_t *settings, BLECharacteristic characteristic) {
	const uint8_t *value = characteristic.value();
	for (int i = 0; i < characteristic.valueLength(); ++i) {
		settings[i] = value[i];
	}
}

void BlinkSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof blinkSettings != characteristic.valueLength()) {
		Serial.println("err blinkSettings");
		return;
	}
#endif
	EffectSettingsChanged(blinkSettings, characteristic);
#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(EEP_ROM_PAGE_SIZE * 1, blinkSettings, sizeof blinkSettings);
	}
#endif
}

void WaveSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof waveSettings != characteristic.valueLength()) {
		Serial.println("err waveSettings");
		return;
	}
#endif
	EffectSettingsChanged(waveSettings, characteristic);
#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(EEP_ROM_PAGE_SIZE * 2, waveSettings, sizeof waveSettings);
	}
#endif
}

void ColorWheelSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof colorWheelSettings != characteristic.valueLength()) {
		Serial.println("err colorWheelSettings");
		return;
	}
#endif
	EffectSettingsChanged(colorWheelSettings, characteristic);
#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(EEP_ROM_PAGE_SIZE * 3, colorWheelSettings, sizeof colorWheelSettings);
	}
#endif
}

void VisualizerSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof visualizerSettings != characteristic.valueLength()) {
		Serial.println("err visualizerSettings");
		return;
	}
#endif
	EffectSettingsChanged(visualizerSettings, characteristic);
#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(EEP_ROM_PAGE_SIZE * 4, visualizerSettings, sizeof visualizerSettings);
	}
#endif
}

void VisorSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof visorSettings != characteristic.valueLength()) {
		Serial.println("err visorSettings");
		return;
	}
#endif
	EffectSettingsChanged(visorSettings, characteristic);
#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(EEP_ROM_PAGE_SIZE * 5, visorSettings, sizeof visorSettings);
	}
#endif
}

void PoliceSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof policeSettings != characteristic.valueLength()) {
		Serial.println("err policeSettings");
		return;
	}
#endif
	EffectSettingsChanged(policeSettings, characteristic);
#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(EEP_ROM_PAGE_SIZE * 6, policeSettings, sizeof policeSettings);
	}
#endif
}

void ChristmasSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof christmasSettings != characteristic.valueLength()) {
		Serial.println("err christmasSettings");
		return;
	}
#endif
	EffectSettingsChanged(christmasSettings, characteristic);
#if EEP_SAVE_CHANGES == 1
	if (eepReady) {
		eep.write(EEP_ROM_PAGE_SIZE * 7, christmasSettings, sizeof christmasSettings);
	}
#endif
}

void AudioDataChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (NUM_AUDIO_POINTS != characteristic.valueLength()) {
		Serial.println("err audioData");
		return;
	}
#endif
	const uint8_t *value = characteristic.value();
	for (int i = 0; i < NUM_AUDIO_POINTS; ++i) {
		float nValue = value[i] / 255.0f;
		if (nValue >= audioData[i]) {
			audioData[i] = nValue;
			audioDecay[i] = false;
		}
		else {
			audioDecay[i] = true;
		}
	}
}

void FnCallChanged(BLEDevice device, BLECharacteristic characteristic) {
#if VALIDATE_BLE_BUFFERS == 1
	if (sizeof functionCallState != characteristic.valueLength()) {
		Serial.println("err functionCallState");
		return;
	}
#endif
	const uint8_t *value = characteristic.value();
	for (int i = 0; i < 1; ++i) {
		if (functionCallState[i] != value[i]) {
			functionCallState[i] = value[i];

			// Call this function
			switch (i) {
			case 0:
				ResetSettings(value[1]);
				break;
			}
		}
	}
}

void ResetSettings(uint8_t effect) {
	switch (effect) {
	case 0:
		blinkSettingsCharacteristic.writeValue(blinkSettingsDefaults, sizeof blinkSettingsDefaults);
		break;

	case 1:
		waveSettingsCharacteristic.writeValue(waveSettingsDefaults, sizeof waveSettingsDefaults);
		break;

	case 2:
		colorWheelSettingsCharacteristic.writeValue(colorWheelSettingsDefaults, sizeof colorWheelSettingsDefaults);
		break;

	case 3:
		visualizerSettingsCharacteristic.writeValue(visualizerSettingsDefaults, sizeof visualizerSettingsDefaults);
		break;

	case 4:
		visorSettingsCharacteristic.writeValue(visorSettingsDefaults, sizeof visorSettingsDefaults);
		break;

	case 5:
		policeSettingsCharacteristic.writeValue(policeSettingsDefaults, sizeof policeSettingsDefaults);
		break;

	case 6:
		christmasSettingsCharacteristic.writeValue(christmasSettingsDefaults, sizeof christmasSettingsDefaults);
		break;
	}
}

void Animate() {

	if (connectionEffectTimer > 0.0f) {
		ConnectionFX();
		return;
	}

	switch (selectedEffect) {
	case 0:
		AnimateBlink();
		break;

	case 1:
		AnimateWave();
		break;

	case 2:
		AnimateColorWheel();
		break;

	case 3:
		AnimateVisualizer();
		break;

	case 4:
		AnimateVisor();
		break;

	case 5:
		AnimatePolice();
		break;

	case 6:
		AnimateChristmas();
		break;
	}
}

// Shared FX
float rainbowCycleTimer = 0.0f;

// Blink FX
float blinkTimer = 0;

void AnimateBlink() {

	blinkTimer += frameTime * (blinkSettings[3] / 10.0f);
	while (blinkTimer >= 1.0f) {
		blinkTimer -= 1.0f;
	}

	const bool shouldFadeIn = blinkSettings[4] != 0;
	const bool shouldFadeOut = blinkSettings[5] != 0;

	float brightness = cos(float(blinkTimer * M_PI * 2 - M_PI)) * 0.5f + 0.5f;
	if (!shouldFadeIn &&
		blinkTimer < 0.5f) {
		brightness = blinkTimer > 0.25f ? 1.0f : 0.0f;
	}
	if (!shouldFadeOut &&
		blinkTimer >= 0.5f) {
		brightness = blinkTimer > 0.75f ? 0.0f : 1.0f;
	}

	if (blinkSettings[6] != 0) {
		const float rainbowSpeed = blinkSettings[7];
		rainbowCycleTimer += frameTime * rainbowSpeed * 2;
		while (rainbowCycleTimer > 360.0f) {
			rainbowCycleTimer -= 360.0f;
		}
		HSV2RGB(rainbowCycleTimer, 100, 100 * brightness, buffer, buffer + 1, buffer + 2);
	}
	else {
		buffer[0] = blinkSettings[1] * brightness;
		buffer[1] = blinkSettings[0] * brightness;
		buffer[2] = blinkSettings[2] * brightness;
	}

	for (int i = NUM_LEDS - 1; i > 0; --i) {
		buffer[i * 3] = buffer[0];
		buffer[i * 3 + 1] = buffer[1];
		buffer[i * 3 + 2] = buffer[2];
	}
	pixels.show();
}

//Wave FX
float waveTimer = 0.0f;

void AnimateWave() {

	waveTimer += frameTime * (waveSettings[10] / 10.0f);
	while (waveTimer >= 1.0f) {
		waveTimer -= 1.0f;
	}

	uint8_t *col1 = waveSettings;
	uint8_t *col2 = waveSettings + 3;
	uint8_t *col3 = waveSettings + 6;
	uint8_t colRainbow[3];

	const bool isRainbow = waveSettings[12] != 0;
	if (isRainbow) {
		col1 = colRainbow;
		col2 = colRainbow;
		col3 = colRainbow;

		const float rainbowSpeed = waveSettings[13];
		rainbowCycleTimer += frameTime * rainbowSpeed * 2;
		while (rainbowCycleTimer > 360.0f) {
			rainbowCycleTimer -= 360.0f;
		}
		HSV2RGB(rainbowCycleTimer, 100, 100, colRainbow, colRainbow + 1, colRainbow + 2);
	}

	const float width = waveSettings[11] * 0.5f;
	for (int i = 0; i < NUM_LEDS; ++i) {
		float brightness = max(0, width - abs((i + 1) - waveTimer * (NUM_LEDS)));
		brightness = max(brightness, width - abs((i + 1) - (waveTimer - 1) * (NUM_LEDS)));
		brightness = max(brightness, width - abs((i + 1) - (waveTimer + 1) * (NUM_LEDS)));
		brightness /= width;

		buffer[i * 3] = col1[1] * brightness;
		buffer[i * 3 + 1] = col1[0] * brightness;
		buffer[i * 3 + 2] = col1[2] * brightness;
	}

	const int numDots = waveSettings[9];
	if (numDots > 1) {
		float dotTimer = waveTimer + ((numDots == 2) ? 0.5f : 0.33333f);

		for (int i = 0; i < NUM_LEDS; ++i) {
			float brightness = max(0, width - abs((i + 1) - dotTimer * (NUM_LEDS)));
			brightness = max(brightness, width - abs((i + 1) - (dotTimer - 1) * (NUM_LEDS)));
			brightness = max(brightness, width - abs((i + 1) - (dotTimer + 1) * (NUM_LEDS)));
			brightness /= width;

			buffer[i * 3] += col2[1] * brightness;
			buffer[i * 3 + 1] += col2[0] * brightness;
			buffer[i * 3 + 2] += col2[2] * brightness;
		}

		if (numDots == 3) {
			dotTimer = waveTimer + 0.66666f;

			for (int i = 0; i < NUM_LEDS; ++i) {
				float brightness = max(0, width - abs((i + 1) - dotTimer * (NUM_LEDS)));
				brightness = max(brightness, width - abs((i + 1) - (dotTimer - 1) * (NUM_LEDS)));
				brightness = max(brightness, width - abs((i + 1) - (dotTimer + 1) * (NUM_LEDS)));
				brightness /= width;

				buffer[i * 3] += col3[1] * brightness;
				buffer[i * 3 + 1] += col3[0] * brightness;
				buffer[i * 3 + 2] += col3[2] * brightness;
			}
		}
	}

	pixels.show();
}

// Color wheel FX
float colorWheelTimer = 0.0f;

void AnimateColorWheel() {

	float brightness = colorWheelSettings[0] / 255.0f;

	colorWheelTimer += frameTime * (colorWheelSettings[1] / 10.0f);
	while (colorWheelTimer >= 1.0f) {
		colorWheelTimer -= 1.0f;
	}

	float width = colorWheelSettings[2] / 10.0f;

	for (int i = 0; i < NUM_LEDS; ++i) {
		uint8_t *led = buffer + (i * 3);

		// Divide by NUM_LEDS and not NUM_LEDS-1 to not duplicate 0/360 degrees
		float hue = fmod((colorWheelTimer + (i / float(NUM_LEDS) * width)), 1.0f) * 360.0f;
		HSV2RGB(hue, 100, 100 * brightness, led, led + 1, led + 2);
	}

	pixels.show();
}

// Audio vis FX
void AnimateVisualizer() {

	for (int i = 0; i < NUM_LEDS; ++i) {
		float brightness = visualizerSettings[0] * audioData[i];
		buffer[i * 3] = brightness;
		buffer[i * 3 + 1] = brightness;
		buffer[i * 3 + 2] = brightness;
	}

	for (int i = 0; i < NUM_AUDIO_POINTS; ++i) {
		if (audioDecay[i]) {
			audioData[i] -= frameTime * visualizerSettings[1] / 10.0f;
			if (audioData[i] < 0.0f) {
				audioData[i] = 0.0f;
			}
		}
	}

	pixels.show();
}

// Visor FX
float visorTimer = 0.0f;

void AnimateVisor() {

	visorTimer += frameTime * (visorSettings[7] / 10.0f);
	while (visorTimer >= 1.0f) {
		visorTimer -= 1.0f;
	}

	const bool fadeIn = visorSettings[8] != 0;
	const bool fadeOut = visorSettings[9] != 0;

	uint8_t *col1 = visorSettings;
	uint8_t *col2 = visorSettings + 3;
	uint8_t colRainbow[3];

	if (visorSettings[6] == 1) {
		col2 = col1;
	}

	const bool isRainbow = visorSettings[10] != 0;
	if (isRainbow) {
		col1 = colRainbow;
		col2 = colRainbow;

		const float rainbowSpeed = visorSettings[11];
		rainbowCycleTimer += frameTime * rainbowSpeed * 2;
		while (rainbowCycleTimer > 360.0f) {
			rainbowCycleTimer -= 360.0f;
		}
		HSV2RGB(rainbowCycleTimer, 100, 100, colRainbow, colRainbow + 1, colRainbow + 2);
	}

	int colorDifferences[3] = {
		col2[0] - col1[0],
		col2[1] - col1[1],
		col2[2] - col1[2]
	};

	for (int i = 0; i < NUM_LEDS; ++i) {
		float periodOffset = i / float(NUM_LEDS) * 0.5f;
		float period = fmod(visorTimer + periodOffset, 1.0f);
		float brightness;
		if (fadeIn && fadeOut) {
			brightness = period > 0.75f ? 1.0f - max(0.0f, (period - 0.75f) * 4.0f) :
				period > 0.5f ? max(0.0f, (period - 0.5f) * 4.0f) :
				0.0f;
		}
		else if (fadeIn) {
			brightness = max(0.0f, (period - 0.5f) * 2.0f);
		}
		else if (fadeOut) {
			brightness = period > 0.5f ? 1.0f - max(0.0f, (period - 0.5f) * 2.0f) : 0.0f;
		}
		else {
			brightness = period > 0.5f ? 1.0f : 0.0f;
		}

		const float colorPeriod = max(0.0f, (period - 0.5f) * 2.0f);
		buffer[i * 3] = (col1[1] + colorDifferences[1] * colorPeriod) * brightness;
		buffer[i * 3 + 1] = (col1[0] + colorDifferences[0] * colorPeriod) * brightness;
		buffer[i * 3 + 2] = (col1[2] + colorDifferences[2] * colorPeriod) * brightness;
	}

	pixels.show();
}

// Police FX
float policeQuickFlashTimer = 0.0f;
float policeBlendTimer = 0.0f;
float policeQuadFlashTimer = 0.0f;
float policeQuadBlendTimer = 0.0f;
float policeQuadMixedFlashTimer = 0.0f;

void _PoliceAddFull(float alpha, const uint8_t *color) {
	alpha = abs(alpha);
	for (int p = 0; p < NUM_LEDS; ++p) {
		buffer[p * 3] += color[1] * alpha;
		buffer[p * 3 + 1] += color[0] * alpha;
		buffer[p * 3 + 2] += color[2] * alpha;
	}
}

void _PoliceAddHalf(float alpha, const uint8_t *color, bool secondHalf) {
	alpha = abs(alpha);
	const int half = ceil(NUM_LEDS / 2.0f);
	int p = secondHalf ? NUM_LEDS - half : 0;
	int end = secondHalf ? NUM_LEDS : half;

	for (; p < end; ++p) {
		buffer[p * 3] += color[1] * alpha;
		buffer[p * 3 + 1] += color[0] * alpha;
		buffer[p * 3 + 2] += color[2] * alpha;
	}
}

void _PoliceAddQuarter(float alpha, const uint8_t *color, int index) {
	alpha = abs(alpha);
	alpha = alpha > 0.5f ? 1.0f : 0.0f;
	const int half = ceil(NUM_LEDS / 2.0f);
	int p = NUM_LEDS / 4.0f * index;
	int end = NUM_LEDS / 4.0f * (index + 1);
	end = min(NUM_LEDS, end);

	for (; p < end; ++p) {
		buffer[p * 3] += color[1] * alpha;
		buffer[p * 3 + 1] += color[0] * alpha;
		buffer[p * 3 + 2] += color[2] * alpha;
	}
}

void AnimatePolice() {

	const uint8_t *col1 = &policeSettings[0];
	const uint8_t *col2 = &policeSettings[3];

	const float ft = frameTime * policeSettings[6] / 10.0f;

	// Zero colors
	for (int i = 0; i < NUM_LEDS; ++i) {
		buffer[i * 3] = 0;
		buffer[i * 3 + 1] = 0;
		buffer[i * 3 + 2] = 0;
	}

	// Flash either side quickly a couple of times
	if (policeQuickFlashTimer < 3.0f * 4.0f) {
		if (int(policeQuickFlashTimer / 3.0f) % 2 == 0) {
			_PoliceAddHalf(sin(policeQuickFlashTimer * float(M_PI)), col1, false);
		}
		else {
			_PoliceAddHalf(sin(policeQuickFlashTimer * float(M_PI)), col2, true);
		}

		policeQuickFlashTimer += ft * 7.0f;
	}
	// Blend between both sides
	else if (policeBlendTimer < 4.0f) {

		if (int(policeBlendTimer) % 2 == 0) {
			_PoliceAddFull(sin(policeBlendTimer * float(M_PI)), col1);
		}
		else {
			_PoliceAddFull(sin(policeBlendTimer * float(M_PI)), col2);
		}

		policeBlendTimer += ft * 2.0f;
	}
	// Flash in two quads in both colors quickly and switch positions
	else if (policeQuadFlashTimer < 6.0f * 3.0f) {
		if (int(policeQuadFlashTimer / 3.0f) % 2 == 0) {
			_PoliceAddQuarter(sin(policeQuadFlashTimer * float(M_PI)), col1, 0);
			_PoliceAddQuarter(sin((policeQuadFlashTimer + 0.5f) * float(M_PI)), col2, 2);
		}
		else {
			_PoliceAddQuarter(sin(policeQuadFlashTimer * float(M_PI)), col1, 3);
			_PoliceAddQuarter(sin((policeQuadFlashTimer + 0.5f) * float(M_PI)), col2, 1);
		}

		policeQuadFlashTimer += ft * 10.0f;
	}
	// Blend between two quads in both colors
	else if (policeQuadBlendTimer < 8.0f) {

		_PoliceAddQuarter(1.0f, int(policeQuadBlendTimer) % 2 ? col1 : col2, int(policeQuadBlendTimer) % 4);

		policeQuadBlendTimer += ft * 2.0f;
	}
	// Keep two quads on and flash two others in both colors
	else if (policeQuadMixedFlashTimer < 3.0f * 4.0f) {
		if (int(policeQuadMixedFlashTimer / 3.0f) % 2 == 0) {
			_PoliceAddQuarter(1.0f, col1, 0);
			_PoliceAddQuarter(1.0f, col1, 2);
			_PoliceAddQuarter(sin(policeQuadMixedFlashTimer * float(M_PI)), col2, 1);
			_PoliceAddQuarter(sin((policeQuadMixedFlashTimer + 0.5f) * float(M_PI)), col2, 3);
		}
		else {
			_PoliceAddQuarter(1.0f, col2, 1);
			_PoliceAddQuarter(1.0f, col2, 3);
			_PoliceAddQuarter(sin(policeQuadMixedFlashTimer * float(M_PI)), col1, 0);
			_PoliceAddQuarter(sin((policeQuadMixedFlashTimer + 0.5f) * float(M_PI)), col1, 2);
		}

		policeQuadMixedFlashTimer += ft * 4.0f;
	}
	// Nothing this frame, reset
	else {
		policeQuickFlashTimer = 0.0f;
		policeBlendTimer = 0.0f;
		policeQuadFlashTimer = 0.0f;
		policeQuadBlendTimer = 0.0f;
		policeQuadMixedFlashTimer = 0.0f;
	}

	pixels.show();
}

// Christmas FX
float christmasTimer = 0.0f;
int christmasLightsPos = 0;
int christmasAccent = 0;

void AnimateChristmas() {

	christmasTimer += frameTime * (christmasSettings[9] / 5.0f);
	while (christmasTimer >= 1.0f) {
		christmasTimer -= 1.0f;
		++christmasLightsPos;
		++christmasAccent;
	}

	christmasLightsPos %= 3;

	uint8_t *col1 = christmasSettings;
	uint8_t *col2 = christmasSettings + 3;
	uint8_t *col3 = christmasSettings + 6;

	if (christmasAccent % 6 == 0) {
		uint8_t *accentColor = christmasAccent % 12 == 0 ? col2 : col3;
		for (int i = 0; i < NUM_LEDS; ++i) {
			buffer[i * 3] = accentColor[1];
			buffer[i * 3 + 1] = accentColor[0];
			buffer[i * 3 + 2] = accentColor[2];
		}
	}
	else {
		// Zero colors
		for (int i = 0; i < NUM_LEDS; ++i) {
			buffer[i * 3] = 0;
			buffer[i * 3 + 1] = 0;
			buffer[i * 3 + 2] = 0;
		}
	}

	// Base Light
	for (int i = christmasLightsPos; i < NUM_LEDS; i += 3) {
		buffer[i * 3] = col1[1];
		buffer[i * 3 + 1] = col1[0];
		buffer[i * 3 + 2] = col1[2];
	}

	pixels.show();
}

void UpdateBLE() {

	bleUpdateTimer -= frameTime;
	if (bleUpdateTimer > 0)
		return;
	bleUpdateTimer = bleCurrentUpdateDelay;

	BLE.poll();
	if (!central || !central.connected())
	{
		if (central) {
			central.disconnect();
		}
		central = BLE.central();
		if (central && central.connected()) {
			connectionEffectTimer = 2.0f;
		}
	}
}

// Connection FX
void ConnectionFX() {

	connectionEffectTimer -= frameTime * 2.5f;
	if (connectionEffectTimer < 0.0f) {
		connectionEffectTimer = 0.0f;
	}

	float brightness = cos(float(connectionEffectTimer * M_PI * 2 - M_PI)) * 0.5f + 0.5f;

	buffer[0] = 32 * brightness;
	buffer[1] = 0 * brightness;
	buffer[2] = 96 * brightness;

	for (int i = 1; i < NUM_LEDS; ++i) {
		buffer[i * 3] = buffer[0];
		buffer[i * 3 + 1] = buffer[1];
		buffer[i * 3 + 2] = buffer[2];
	}
	pixels.show();
}
