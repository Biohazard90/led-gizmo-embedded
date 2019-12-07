#include <ArduinoBLE.h>
#include <Adafruit_NeoPixel.h>

#include "ColorUtilities.h"

#define PIN 2
#define NUM_LEDS 5
#define ANIMATION_DELAY int(1000/30) // FPS
#define BLE_DELAY 0.25f

Adafruit_NeoPixel pixels(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);

// GRB format
uint8_t *buffer;

uint8_t blinkSettings[8] = {
	0xFF, 0, 0, // Color
	10, // Speed
	0, // Fade in
	1, // Fade out
	0, // Rainbow cycle
	10, // Rainbow cycle speed
};

uint8_t waveSettings[9 + 7] = {
	0xFF, 0, 0, // Color 1
	0xFF, 0, 0, // Color 2
	0xFF, 0, 0, // Color 3
	1, // Number colors

	10, // Speed
	1, // Width
	0, // Fade in
	1, // Fade out
	0, // Rainbow cycle
	10, // Rainbow cycle speed
};

BLEService ledService("e8942ca1-eef7-4a95-afeb-e3d07e8af52e");

BLEIntCharacteristic effectTypeCharacteristic("e8942ca1-d9e7-4c45-b96c-10cf850bfa00", BLERead | BLEWrite);
BLECharacteristic blinkSettingsCharacteristic("e8942ca1-d9e7-4c45-b96c-10cf850bfa01", BLERead | BLEWrite, sizeof blinkSettings);
BLECharacteristic waveSettingsCharacteristic("e8942ca1-d9e7-4c45-b96c-10cf850bfa02", BLERead | BLEWrite, sizeof blinkSettings);

// BLEIntCharacteristic event handler BLEWritten

void setup() {
	//Serial.begin(9600);

	pixels.begin();
	buffer = pixels.getPixels();

	// BLE setup
	BLE.begin();
	BLE.setLocalName("Doggy Light");

	// Service setup
	ledService.addCharacteristic(effectTypeCharacteristic);
	ledService.addCharacteristic(blinkSettingsCharacteristic);
	ledService.addCharacteristic(waveSettingsCharacteristic);
	BLE.addService(ledService);

	// Characteristics init
	effectTypeCharacteristic.writeValue(0);
	blinkSettingsCharacteristic.writeValue(blinkSettings, sizeof blinkSettings);
	waveSettingsCharacteristic.writeValue(waveSettings, sizeof waveSettings);

	// Characteristics callbacks
	effectTypeCharacteristic.setEventHandler(BLEWritten, EffectTypeChanged);
	blinkSettingsCharacteristic.setEventHandler(BLEWritten, BlinkSettingsChanged);
	waveSettingsCharacteristic.setEventHandler(BLEWritten, WaveSettingsChanged);

	// Advertise
	BLE.setAdvertisedService(ledService);
	BLE.setAdvertisingInterval(320); // 160 == 100ms
	BLE.advertise();

	BLE.setEventHandler(BLEConnected, blePeripheralConnectedHandler);
	BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectedHandler);
}

int lastTime = 0;
int animationDelayCompensation = 0;
float frameTime = 0.0f;
float bleUpdateTimer = 0;
float bleCurrentUpdateDelay = BLE_DELAY;

BLEDevice central;

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

int animationType = 0;

//int brightness = 0;
//int d = 0;

//uint8_t colG = 0;
//uint8_t colR = 255;
//uint8_t colB = 0;

void EffectTypeChanged(BLEDevice device, BLECharacteristic characteristic) {
	animationType = *(const int*)characteristic.value();
}

void BlinkSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
	const uint8_t *value = characteristic.value();
	for (int i = 0; i < characteristic.valueLength(); ++i) {
		blinkSettings[i] = value[i];
	}
}

void WaveSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
	const uint8_t *value = characteristic.value();
	for (int i = 0; i < characteristic.valueLength(); ++i) {
		waveSettings[i] = value[i];
	}
}

void Animate() {

	//AnimateBlink();
	AnimateWave();

	//int val = brightness * 3 - 255 * 2;
	//if (val < 0)
	//	val = 0;
	//buffer[0] = colG * val / 255.0f;
	//buffer[1] = colR * val / 255.0f;
	//buffer[2] = colB * val / 255.0f;
	//for (int i = NUM_LEDS - 1; i > 0; --i) {
	//	buffer[i * 3] = buffer[(i - 1) * 3];
	//	buffer[i * 3 + 1] = buffer[(i - 1) * 3 + 1];
	//	buffer[i * 3 + 2] = buffer[(i - 1) * 3 + 2];
	//}
	//pixels.show();
	//brightness += d ? -20 : 20;
	//if (brightness > 255)
	//{
	//	brightness = 255;
	//	d = 1;
	//}
	//if (brightness < 0)
	//{
	//	brightness = 0;
	//	d = 0;
	//}
}

// Blink FX
float blinkTimer = 0;
float rainbowCycleTimer = 0.0f;

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

	waveTimer += frameTime * (waveSettings[8] / 10.0f);
	while (waveTimer >= 1.0f) {
		waveTimer -= 1.0f;
	}

	for (int i = NUM_LEDS - 1; i > 0; --i) {
		buffer[i * 3] = buffer[(i - 1) * 3];
		buffer[i * 3 + 1] = buffer[(i - 1) * 3 + 1];
		buffer[i * 3 + 2] = buffer[(i - 1) * 3 + 2];
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
		//Serial.println("Connecting new central");
		central = BLE.central();
	}

	//if (central) {  // if a central is connected to peripheral:
	//	while (blinkSettingsCharacteristic.written()) {
	//		const uint8_t *newValue = blinkSettingsCharacteristic.value();
	//		colR = (ledCharacteristicValue & 0xFF);
	//		colG = ((ledCharacteristicValue >> 8) & 0xFF);
	//		colB = ((ledCharacteristicValue >> 16) & 0xFF);
	//	}
	//}
}