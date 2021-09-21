
#include <ArduinoBLE.h>

#include "gizmoled.h"

#include "blesenseflash.h"

//#include <Wire.h>
//#include <extEEPROM.h>

using namespace GizmoLED;

//#define LED_PIN 2
//#define NUM_LEDS 101

#define BLE_DELAY 0.1f
#define BLE_DELAY_VISUALIZER 0.0f
#define EEP_ROM_PAGE_SIZE 128
#define CONNECTION_FX_TIME 1.5f
#define AUDIO_HOLD_TIME 10.0f

// Persisted data
struct Generic
{
	uint8_t selectedEffect = 0;
	uint8_t selectedEffectSecondary = 0;
	uint8_t numberOfEffects = 0;
	uint8_t effectNames[MAX_NUMBER_EFFECTS] = { 0 };
	bool isInitialized = false;
};

#define MAX_DEVICE_NAME 32
#define FLASH_DEVICE_NAME_OFFSET 1
#define FLASH_EFFECT_BASE_OFFSET 2

Generic genericData;
char deviceName[MAX_DEVICE_NAME];

constexpr int flashBlockSize = BLEFLASH_BLOCK_SIZE(EEP_ROM_PAGE_SIZE * (2 + MAX_NUMBER_EFFECTS));
BLEFLASH_DECLARE_VARIABLE(flashAll, flashBlockSize) = {};
BLEFLASH_DECLARE_ACCESS(uint8_t, flashGeneric, flashAll, 0);
BLEFLASH_DECLARE_ACCESS(uint8_t, flashDeviceName, flashAll, FLASH_DEVICE_NAME_OFFSET * EEP_ROM_PAGE_SIZE);
BLEFLASH_DECLARE_ACCESS(uint8_t, flashEffectBase, flashAll, FLASH_EFFECT_BASE_OFFSET * EEP_ROM_PAGE_SIZE);

// Temp data
namespace GizmoLED
{
	float audioData[NUM_AUDIO_POINTS] = { 0.0f };
	FnConnectionAnimation connectionAnimation = nullptr;
	FnEffectChangedCallback effectChangedCallback = nullptr;
}

#define MAX_FNCALL_ARGS 32
uint8_t functionCallState[] =
{
	0, // Change trigger
	0, // Function index
	// var args in characteristic
};

extern const char *defaultDeviceName;
//const char *defaultDeviceName = PROGMEM "Collar";

// BLE
const char *serviceID = PROGMEM "e8942ca1-eef7-4a95-afeb-e3d07e8af52e";
BLEService ledService(serviceID);
BLEService ledServiceAD(serviceID);
BLEService ledServiceADV(PROGMEM "e8942ca1-eef7-4a95-afeb-e3d07e8af52d");
BLECharacteristic effectTypeCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfb00", BLERead | BLEWrite, sizeof(struct Generic));

// Upstream BLE
BLECharacteristic audioDataCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-20cf850bfa00", BLEWrite | BLEWriteWithoutResponse, sizeof audioData);
BLECharacteristic fnCallCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-20cf850bfa01", BLEWrite, sizeof functionCallState + MAX_FNCALL_ARGS);

// EEP
//extEEPROM eep(kbits_256, 1, EEP_ROM_PAGE_SIZE);
//bool eepReady = false;

#define EEP_LOAD 0
#define EEP_TEST 0
#define EEP_SAVE_CHANGES 0

Effect *effects = nullptr;
int numEffects = 0;

int lastTime = 0;
int animationDelayCompensation = 0;
float frameTime = 0.0f;
float bleUpdateTimer = 0;
float bleCurrentUpdateDelay = BLE_DELAY;

float connectionEffectTimer = 0.0f;
float lastAudioTime = 0.0f;
float settingsDirtyTimer = 0.0f;

BLEDevice central;

void SetVisualizerInputSupported(bool isSupported)
{
	BLE.setAdvertisedService(isSupported ? ledServiceADV : ledServiceAD);
}

void MakeSettingsDirty()
{
	settingsDirtyTimer = 5.0f;
}

void EffectTypeChanged(BLEDevice device, BLECharacteristic characteristic)
{
	uint8_t effectIndex = *(const uint8_t*)characteristic.value();
	if (effectIndex >= numEffects) {
		return;
	}
	
	Effect &lastEffect = effects[genericData.selectedEffect];

	genericData.selectedEffect = effectIndex;

	Effect &effect = effects[effectIndex];
	if (effect.type != EFFECTTYPE_VISUALIZER)
	{
		genericData.selectedEffectSecondary = genericData.selectedEffect;
	}

	BLE.stopAdvertise();
	SetVisualizerInputSupported(effect.type == EFFECTTYPE_VISUALIZER);
	BLE.advertise();

#if EEP_SAVE_CHANGES == 1
	if (eepReady)
	{
		eep.write(0, (byte*)&genericData, sizeof genericData);
	}
#endif

	bleCurrentUpdateDelay = (effect.type == EFFECTTYPE_VISUALIZER) ? BLE_DELAY_VISUALIZER : BLE_DELAY;
	
	MakeSettingsDirty();
	
	if (effectChangedCallback != nullptr)
	{
		effectChangedCallback(effect.name, lastEffect.name);
	}
}

void EffectSettingsChanged(BLEDevice device, BLECharacteristic characteristic)
{
	//Serial.println("effect changed");
	Effect *effect = nullptr;
	for (int i = 0; i < numEffects; ++i)
	{
		Effect *_effect = &effects[i];
		if (strcasecmp(characteristic.uuid(), _effect->characteristic->uuid()) == 0)
		{
			effect = _effect;
			//Serial.println("Select effect at index " + String(i));

			const char *cRead = characteristic.uuid();
			while (*cRead)
				++cRead;
			int len = cRead - characteristic.uuid();

			//Serial.println("UUID length " + String(len) + ", last value: " + String((int)*(cRead - 1)));
			break;
		}
	}

	if (effect == nullptr)
	{
		//Serial.println("no effect");
		return;
	}

	if (effect->settingsSize != characteristic.valueLength())
	{
		//Serial.println(String(effect->name) + " wrong size: " + String(effect->settingsSize) + " != " + String(characteristic.valueLength()));
		return;
	}

	//Serial.println("Changing characteristic: " + String(effect->name));
	const uint8_t *value = characteristic.value();
	for (int i = 0; i < characteristic.valueLength(); ++i)
	{
		effect->settings[i] = value[i];
		//Serial.println("v: " + String(value[i]));
	}
	
	MakeSettingsDirty();

	//if (EEP_SAVE_CHANGES == 1)
	//{
	//	if (eepReady)
	//	{
	//		eep.write(EEP_ROM_PAGE_SIZE * effect->eepOffset, effect->settings, effect->settingsSize);
	//	}
	//}
}

void copySmall(uint8_t *dst, uint8_t *src, int size)
{
	--size;
	for (; size >= 0; --size)
	{
		dst[size] = src[size];
	}
}

void ResetSettings(uint8_t effectIndex)
{
	if (effectIndex >= numEffects)
		return;

	Effect &effect = effects[effectIndex];
	effect.characteristic->writeValue(effect.defaultSettings, effect.settingsSize);
}

void RenameDevice(const uint8_t *args, int len)
{
	if (len < 1)
	{
		// Clear name from storage
		uint8_t noName[MAX_DEVICE_NAME] = { 0 };
		memcpy(deviceName, noName, sizeof noName);

		BLE.setLocalName(defaultDeviceName);
	}
	else
	{
		len = min(MAX_DEVICE_NAME - 1, len);
		memcpy(deviceName, args, len);
		deviceName[len] = 0;

		BLE.stopAdvertise();
		BLE.setLocalName((const char*)deviceName);
		BLE.advertise();
	}
	
	MakeSettingsDirty();
	//if (eepReady)
	//	eep.write(EEP_ROM_PAGE_SIZE * EEP_DEVICE_NAME_OFFSET, (byte*)userDeviceName, sizeof userDeviceName);
}

//int lastAudioTime = 0;
//int audioFrame = 0;
void AudioDataChanged(BLEDevice device, BLECharacteristic characteristic)
{
	if (1 != characteristic.valueLength())
	{
		return;
	}

	//int audioTime = millis();
	//int d = audioTime - lastAudioTime;
	//lastAudioTime = audioTime;
	//Serial.println("Audio lag: " + String(d) + ", frame: " + String(audioFrame));
	//++audioFrame;

	//const uint8_t *value = characteristic.value();
	const uint8_t d = *characteristic.value();

	bool anyAudioReceived = false;
	for (int i = 0; i < NUM_AUDIO_POINTS; ++i)
	{
		float nValue = ((d >> i) & 0x1) ? 1.0f : 0.0f;
		//if (nValue >= audioData[i])
		//{
		audioData[i] = nValue;
		//	audioDecay[i] = false;
		//}
		//else {
		//	audioDecay[i] = true;
		//}

		anyAudioReceived = anyAudioReceived || nValue > 0.0f;
	}

	if (anyAudioReceived)
	{
		lastAudioTime = AUDIO_HOLD_TIME;
	}
}

void FnCallChanged(BLEDevice device, BLECharacteristic characteristic)
{
	const int fnStateLength = sizeof functionCallState;
	const int dataLength = characteristic.valueLength() - fnStateLength;
	if (characteristic.valueLength() < fnStateLength)
	{
		return;
	}

	const uint8_t *value = characteristic.value();
	if (functionCallState[0] != value[0])
	{
		functionCallState[0] = value[0];

		// Call this function
		switch (value[1])
		{
		case 0:
		{
			if (dataLength >= 1)
			{
				ResetSettings(value[2]);
			}
		}
		break;

		case 1:
		{
			int nameLength = characteristic.valueLength() - fnStateLength;
			RenameDevice(characteristic.value() + fnStateLength, nameLength);
		}
		break;
		}
	}
}

// Connection FX
void ConnectionFX()
{
	if (connectionAnimation != nullptr)
	{
		connectionAnimation(frameTime, (CONNECTION_FX_TIME - connectionEffectTimer) / CONNECTION_FX_TIME);
	}
}

void Animate()
{
	if (connectionEffectTimer > 0.0f)
	{
		ConnectionFX();

		connectionEffectTimer -= frameTime;
		if (connectionEffectTimer < 0.0f)
		{
			connectionEffectTimer = 0.0f;
		}
	}
	else
	{
		if (genericData.selectedEffect >= 0 && genericData.selectedEffect < numEffects)
		{
			Effect *effect = &effects[genericData.selectedEffect];

			// Only show visualizer if audio is playing
			if (effect->type == EFFECTTYPE_VISUALIZER)
			{
				if (lastAudioTime > 0.0f)
				{
					lastAudioTime -= frameTime;
				}

				if (lastAudioTime <= 0.0f)
				{
					lastAudioTime = 0.0f;
					if (genericData.selectedEffectSecondary >= 0 && genericData.selectedEffectSecondary < numEffects)
					{
						effect = &effects[genericData.selectedEffectSecondary];
					}
					else
					{
						effect = nullptr;
					}
				}
			}

			if (effect != nullptr)
			{
				effect->fnEffectAnimation(frameTime);
			}
		}
	}
}

void UpdateBLE()
{
	bleUpdateTimer -= frameTime;
	if (bleUpdateTimer > 0)
		return;

	bleUpdateTimer = bleCurrentUpdateDelay;

	//Serial.println(String("update ble " + String(millis())));
	BLE.poll();

	//central = BLE.central();
	//if (central)
	//{
	//	int rssi = central.rssi();
	//	
	//	Serial.println(String("rssi " + String(rssi)));
	//	//if (rssi > 0 &&
	//	//	rssi != 0)
	//	//{
	//	//	Serial.println(String("rssi dropped ") + String(rssi));
	//	//	central.disconnect();
	//	//}
	//}
	//if (!central || !central.connected())
	//{
	//	if (central)
	//	{
	//		central.disconnect();
	//	}

	//	central = BLE.central();
	//	if (central && central.connected())
	//	{
	//		//Serial.println("connected central: " + central.deviceName() + central.localName());
	//		connectionEffectTimer = CONNECTION_FX_TIME;

	//		// Reset function call trigger
	//		functionCallState[0] = 0;
	//	}
	//}
}

void blePeripheralConnectedHandler(BLEDevice device)
{
	//Serial.print("Connected event, device: ");
	//Serial.println(device.address());

	//Serial.println("connected central: " + central.deviceName() + central.localName());
	connectionEffectTimer = CONNECTION_FX_TIME;

	// Reset function call trigger
	functionCallState[0] = 0;
}
//
//void blePeripheralDisconnectedHandler(BLEDevice device) {
//	//Serial.print("Disconnected event, device: ");
//	//Serial.println(device.address());
//}

void StoreCurrentSettings()
{
	//Serial.println("Flashing settings...");

	uint8_t *data = (uint8_t*)malloc(flashBlockSize);

	genericData.isInitialized = true;
	copySmall(data, (uint8_t*)&genericData, sizeof(struct Generic));
	copySmall(data + FLASH_DEVICE_NAME_OFFSET * EEP_ROM_PAGE_SIZE, (uint8_t*)deviceName, MAX_DEVICE_NAME);
	for (int i = 0; i < numEffects; ++i)
	{
		Effect &effect = effects[i];
		copySmall(data + (FLASH_EFFECT_BASE_OFFSET + i) * EEP_ROM_PAGE_SIZE, effect.settings, effect.settingsSize);
	}
	
	BLEFLASH_WRITE(flashAll, flashBlockSize, data);

	free(data);
}

void GizmoLEDSetup()
{
	//Serial.begin(115200);
	//Serial.setTimeout(50);


	// BLE init
	BLE.setConnectionInterval(0x0001, 0x0001);
	BLE.begin();
	BLE.setLocalName(defaultDeviceName);

	for (int i = 0; i < numEffects; ++i)
	{
		Effect &effect = effects[i];
		effect.defaultSettings = new uint8_t[effect.settingsSize];
		copySmall(effect.defaultSettings, effect.settings, effect.settingsSize);
	}

	// Flash init
	if (((Generic*)flashGeneric)->isInitialized)
	{
		//Serial.println("Init from flash");
		
		// Load all settings from flash memory
		copySmall((uint8_t*)&genericData, flashGeneric, sizeof(struct Generic));
		
		if (*flashDeviceName != 0)
		{
			copySmall((uint8_t*)deviceName, flashDeviceName, MAX_DEVICE_NAME - 1);
		}

		// Load effect settings
		for (int i = 0; i < numEffects; ++i)
		{
			Effect &effect = effects[i];
			uint8_t *flashEffectSettings = flashEffectBase + EEP_ROM_PAGE_SIZE * i;
			copySmall(effect.settings, flashEffectSettings, effect.settingsSize);
		}
		
		if (*deviceName != 0)
		{
			BLE.setLocalName((const char*)deviceName);
		}
	}
	else
	{
		// If isInitialized isn't set, we need to initialize all settings based on their default settings because the flash is empty
		//Serial.println("Init defaults");
	}

	// EEP init
	//if (eep.begin(eep.twiClock400kHz) == 0)
	{
		//eepReady = true;

		//Generic tempGeneric;
		//eep.read(0, (byte*)&tempGeneric, sizeof tempGeneric);
		//if (tempGeneric.romVersion != genericData.romVersion)
		//{
		//	// ROM mismatch, init all data
		//	eep.write(0, (byte*)&genericData, sizeof genericData);

		//	uint8_t noName = 0;
		//	eep.write(EEP_ROM_PAGE_SIZE * 1, (byte*)&noName, sizeof noName);
		//	for (int e = 0; e < numEffects; ++e)
		//	{
		//		Effect &effect = effects[e];
		//		eep.write(EEP_ROM_PAGE_SIZE * effect.eepOffset, (byte*)effect.settings, effect.settingsSize);
		//	}
		//}

#if EEP_LOAD == 1
		eep.read(0, (byte*)&genericData, sizeof genericData);
		for (int e = 0; e < numEffects; ++e)
		{
			Effect &effect = effects[e];
			eep.read(EEP_ROM_PAGE_SIZE * effect.eepOffset, (byte*)effect.settings, effect.settingsSize);
		}

		eep.read(EEP_ROM_PAGE_SIZE * EEP_DEVICE_NAME_OFFSET, (byte*)&userDeviceName, MAX_DEVICE_NAME - 1);
		if (*userDeviceName != 0)
		{
			BLE.setLocalName((const char*)userDeviceName);
		}
#endif

#if EEP_TEST == 1
		Serial.println("eep success");
		if (eep.write(0, (byte*)&genericData.selectedEffect, 1) != 0)
		{
			Serial.println("eep write err");
		}

		byte readTest = 255;
		if (eep.read(0, &readTest, 1) == 0)
		{
			Serial.println(String("eep read success: ") + String(readTest));
		}
		else
		{
			Serial.println("eep read err");
		}
#endif
	}

	bleCurrentUpdateDelay = (effects[genericData.selectedEffect].type == EFFECTTYPE_VISUALIZER) ? BLE_DELAY_VISUALIZER : BLE_DELAY;

	//genericData.visualizerFlags = 0;
	genericData.numberOfEffects = numEffects;
	for (int i = 0; i < numEffects; ++i)
	{
		Effect &effect = effects[i];
		genericData.effectNames[i] = effect.name;
		//if (effect.type == EFFECTTYPE_VISUALIZER)
		//	genericData.visualizerFlags |= (1 << i);
	}
	//else {
	//	Serial.println("eep err");
	//}
	
	//Serial.println("Num effects " + String(genericData.numberOfEffects) + ", update " + String(sizeof(struct Generic)));

	for (int e = 0; e < numEffects; ++e)
	{
		Effect &effect = effects[e];
		ledService.addCharacteristic(*effect.characteristic);
		effect.characteristic->writeValue(effect.settings, effect.settingsSize);
		effect.characteristic->setEventHandler(BLEWritten, EffectSettingsChanged);
	}

	// Service setup
	ledService.addCharacteristic(effectTypeCharacteristic);
	ledService.addCharacteristic(audioDataCharacteristic);
	ledService.addCharacteristic(fnCallCharacteristic);

	// Characteristics init
	effectTypeCharacteristic.writeValue((byte*)&genericData, sizeof(struct Generic));
	fnCallCharacteristic.writeValue(functionCallState, sizeof functionCallState);

	// Characteristics callbacks
	effectTypeCharacteristic.setEventHandler(BLEWritten, EffectTypeChanged);
	audioDataCharacteristic.setEventHandler(BLEWritten, AudioDataChanged);
	fnCallCharacteristic.setEventHandler(BLEWritten, FnCallChanged);

	// Advertise
	BLE.addService(ledService);
	//BLE.setAdvertisedService(ledService);

	SetVisualizerInputSupported(genericData.selectedEffect < numEffects &&
		effects[genericData.selectedEffect].type == EFFECTTYPE_VISUALIZER);

	BLE.setAdvertisingInterval(320); // 160 == 100ms
	BLE.advertise();

	BLE.setEventHandler(BLEConnected, blePeripheralConnectedHandler);
	//BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectedHandler);
	
	if (effectChangedCallback != nullptr)
	{
		effectChangedCallback(effects[genericData.selectedEffect].name, -1);
	}
}

void GizmoLEDLoop()
{
	int currentTime = millis();
	frameTime = (currentTime - lastTime) / 1000.0f;
	int animationDelay = lastTime = currentTime;

	if (frameTime >= 1.0f)
	{
		frameTime = 0.999f;
	}

	Animate();

	UpdateBLE();
	
	if (settingsDirtyTimer > 0.0f)
	{
		settingsDirtyTimer -= frameTime;
		if (settingsDirtyTimer <= 0.0f)
		{
			StoreCurrentSettings();
		}
	}

	animationDelay = ANIMATION_DELAY - (millis() - animationDelay);
	if (animationDelay < 0)
	{
		animationDelay = 0;
	}
	else if (animationDelay > ANIMATION_DELAY)
	{
		animationDelay = ANIMATION_DELAY;
	}
	delay(animationDelay);
}

//void EffectSettingsChanged(uint8_t *settings, BLECharacteristic characteristic) {
//	const uint8_t *value = characteristic.value();
//	for (int i = 0; i < characteristic.valueLength(); ++i) {
//		settings[i] = value[i];
//	}
//}
//
//void BlinkSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
//#if VALIDATE_BLE_BUFFERS == 1
//	if (sizeof blinkSettings != characteristic.valueLength()) {
//		Serial.println("err blinkSettings");
//		return;
//	}
//#endif
//	EffectSettingsChanged(blinkSettings, characteristic);
//#if EEP_SAVE_CHANGES == 1
//	if (eepReady) {
//		eep.write(EEP_ROM_PAGE_SIZE * 1, blinkSettings, sizeof blinkSettings);
//	}
//#endif
//}
//
//void WaveSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
//#if VALIDATE_BLE_BUFFERS == 1
//	if (sizeof waveSettings != characteristic.valueLength()) {
//		Serial.println("err waveSettings");
//		return;
//	}
//#endif
//	EffectSettingsChanged(waveSettings, characteristic);
//#if EEP_SAVE_CHANGES == 1
//	if (eepReady) {
//		eep.write(EEP_ROM_PAGE_SIZE * 2, waveSettings, sizeof waveSettings);
//	}
//#endif
//}
//
//void ColorWheelSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
//#if VALIDATE_BLE_BUFFERS == 1
//	if (sizeof colorWheelSettings != characteristic.valueLength()) {
//		Serial.println("err colorWheelSettings");
//		return;
//	}
//#endif
//	EffectSettingsChanged(colorWheelSettings, characteristic);
//#if EEP_SAVE_CHANGES == 1
//	if (eepReady) {
//		eep.write(EEP_ROM_PAGE_SIZE * 3, colorWheelSettings, sizeof colorWheelSettings);
//	}
//#endif
//}
//
//void VisualizerSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
//#if VALIDATE_BLE_BUFFERS == 1
//	if (sizeof visualizerSettings != characteristic.valueLength()) {
//		Serial.println("err visualizerSettings");
//		return;
//	}
//#endif
//	EffectSettingsChanged(visualizerSettings, characteristic);
//#if EEP_SAVE_CHANGES == 1
//	if (eepReady) {
//		eep.write(EEP_ROM_PAGE_SIZE * 4, visualizerSettings, sizeof visualizerSettings);
//	}
//#endif
//}
//
//void VisorSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
//#if VALIDATE_BLE_BUFFERS == 1
//	if (sizeof visorSettings != characteristic.valueLength()) {
//		Serial.println("err visorSettings");
//		return;
//	}
//#endif
//	EffectSettingsChanged(visorSettings, characteristic);
//#if EEP_SAVE_CHANGES == 1
//	if (eepReady) {
//		eep.write(EEP_ROM_PAGE_SIZE * 5, visorSettings, sizeof visorSettings);
//	}
//#endif
//}
//
//void PoliceSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
//#if VALIDATE_BLE_BUFFERS == 1
//	if (sizeof policeSettings != characteristic.valueLength()) {
//		Serial.println("err policeSettings");
//		return;
//	}
//#endif
//	EffectSettingsChanged(policeSettings, characteristic);
//#if EEP_SAVE_CHANGES == 1
//	if (eepReady) {
//		eep.write(EEP_ROM_PAGE_SIZE * 6, policeSettings, sizeof policeSettings);
//	}
//#endif
//}
//
//void ChristmasSettingsChanged(BLEDevice device, BLECharacteristic characteristic) {
//#if VALIDATE_BLE_BUFFERS == 1
//	if (sizeof christmasSettings != characteristic.valueLength()) {
//		Serial.println("err christmasSettings");
//		return;
//	}
//#endif
//	EffectSettingsChanged(christmasSettings, characteristic);
//#if EEP_SAVE_CHANGES == 1
//	if (eepReady) {
//		eep.write(EEP_ROM_PAGE_SIZE * 7, christmasSettings, sizeof christmasSettings);
//	}
//#endif
//}


/*

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
}

// Audio vis FX
float audioPosition = 0.0f;

void AnimateVisualizer() {

	uint8_t *colBase = visualizerSettings;
	uint8_t *colPoints[] = {
		visualizerSettings + 3,
		visualizerSettings + 3,
		visualizerSettings + 3,
		visualizerSettings + 3,
		visualizerSettings + 3
	};
	uint8_t colRainbow[6 * 3];

	const bool isRainbow = visualizerSettings[9] != 0;
	if (isRainbow) {
		colBase = colRainbow;
		colPoints[0] = colRainbow + 3;
		colPoints[1] = colRainbow + 6;
		colPoints[2] = colRainbow + 9;
		colPoints[3] = colRainbow + 12;
		colPoints[4] = colRainbow + 15;

		const float rainbowSpeed = visualizerSettings[10];
		rainbowCycleTimer += frameTime * rainbowSpeed * 2;
		while (rainbowCycleTimer > 360.0f) {
			rainbowCycleTimer -= 360.0f;
		}

		const float stepOffset = 360.0f / 6.0f;
		HSV2RGB(rainbowCycleTimer, 100, 100, colRainbow);
		HSV2RGB(fmod(rainbowCycleTimer + stepOffset, 360.0f), 100, 100, colRainbow + 3);
		HSV2RGB(fmod(rainbowCycleTimer + stepOffset * 2, 360.0f), 100, 100, colRainbow + 6);
		HSV2RGB(fmod(rainbowCycleTimer + stepOffset * 3, 360.0f), 100, 100, colRainbow + 9);
		HSV2RGB(fmod(rainbowCycleTimer + stepOffset * 4, 360.0f), 100, 100, colRainbow + 12);
		HSV2RGB(fmod(rainbowCycleTimer + stepOffset * 5, 360.0f), 100, 100, colRainbow + 15);
	}

	const float baseBrightness = visualizerSettings[6] / 255.0f;

	// Rotate lights
	audioPosition += visualizerSettings[8] / 10.0f * frameTime;
	while (audioPosition >= 1.0f) {
		audioPosition -= 1.0f;
	}

	// Apply points
	const int numFloatingPoints = NUM_AUDIO_POINTS - 1;
	for (int i = 0; i < NUM_LEDS; ++i) {
		const float ledPosition = fmod(i / float(NUM_LEDS) + audioPosition, 1.0f);
		const int visPoint1 = floor(ledPosition * numFloatingPoints);
		const int visPoint2 = (visPoint1 + 1) % numFloatingPoints;
		const float visPoint1Amt = 1.0f - fmod(ledPosition * numFloatingPoints, 1.0f);
		const float visPoint2Amt = 1.0f - visPoint1Amt;

		const float brightness1 = baseBrightness * audioData[visPoint1 + 1] * visPoint1Amt;
		const float brightness2 = baseBrightness * audioData[visPoint2 + 1] * visPoint2Amt;

		buffer[i * 3] = min(255, brightness1 * colPoints[visPoint1][1] + brightness2 * colPoints[visPoint2][1] +
			audioData[0] * baseBrightness * colBase[1]);
		buffer[i * 3 + 1] = min(255, brightness1 * colPoints[visPoint1][0] + brightness2 * colPoints[visPoint2][0] +
			audioData[0] * baseBrightness * colBase[0]);
		buffer[i * 3 + 2] = min(255, brightness1 * colPoints[visPoint1][2] + brightness2 * colPoints[visPoint2][2] +
			audioData[0] * baseBrightness * colBase[2]);
	}

	// Decay audio light
	for (int i = 0; i < NUM_AUDIO_POINTS; ++i) {
		if (audioDecay[i]) {
			audioData[i] -= frameTime * visualizerSettings[7] / 10.0f;
			if (audioData[i] < 0.0f) {
				audioData[i] = 0.0f;
			}
		}
	}
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
	int p = NUM_LEDS / 4.0f * index;
	int end = NUM_LEDS / 4.0f * (index + 1);
	end = min(NUM_LEDS, end);

	for (; p < end; ++p) {
		buffer[p * 3] += color[1] * alpha;
		buffer[p * 3 + 1] += color[0] * alpha;
		buffer[p * 3 + 2] += color[2] * alpha;
	}
}

void _PoliceAddAnimatedStrike(const uint8_t *color, int size, float time, bool secondHalf) {
	int p = 0;
	int end = NUM_LEDS;
	time *= (NUM_LEDS + size) / float(NUM_LEDS);

	for (; p < end; ++p) {
		const float minThreshold = p / float(NUM_LEDS);
		const float maxThreshold = (p + size) / float(NUM_LEDS);

		if (time > minThreshold &&
			time < maxThreshold) {
			buffer[p * 3] += color[1];
			buffer[p * 3 + 1] += color[0];
			buffer[p * 3 + 2] += color[2];
		}
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
	else if (policeQuadBlendTimer < 4.0f) {
		if (int(policeQuadBlendTimer) % 2 == 0) {
			_PoliceAddAnimatedStrike(col1, NUM_LEDS / 5, fmod(policeQuadBlendTimer, 1.0f), false);
		}
		else {
			_PoliceAddAnimatedStrike(col2, NUM_LEDS / 5, 1.0f - fmod(policeQuadBlendTimer, 1.0f), true);
		}

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
}

// Christmas FX
float christmasTimer = 0.0f;
int christmasLightsPos = 0;
int christmasAccent = 0;
float accentAlpha = 0.0f;
uint8_t *accentColor = nullptr;

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

	uint8_t activeColor[] = {
		col1[0],
		col1[1],
		col1[2]
	};

	if (christmasAccent % 6 == 0) {
		accentAlpha = 1.0f;
		accentColor = christmasAccent % 12 == 0 ? col2 : col3;
	}

	if (accentAlpha > 0.0f) {
		activeColor[0] = activeColor[0] + (accentColor[0] - activeColor[0]) * accentAlpha;
		activeColor[1] = activeColor[1] + (accentColor[1] - activeColor[1]) * accentAlpha;
		activeColor[2] = activeColor[2] + (accentColor[2] - activeColor[2]) * accentAlpha;

		accentAlpha -= frameTime * (christmasSettings[10] / 10.0f);
		if (accentAlpha < 0.0f) {
			accentAlpha = 0.0f;
		}
	}

	// Zero colors
	for (int i = 0; i < NUM_LEDS; ++i) {
		buffer[i * 3] = 0;
		buffer[i * 3 + 1] = 0;
		buffer[i * 3 + 2] = 0;
	}

	// Base Light
	for (int i = christmasLightsPos; i < NUM_LEDS; i += 3) {
		buffer[i * 3] = activeColor[1];
		buffer[i * 3 + 1] = activeColor[0];
		buffer[i * 3 + 2] = activeColor[2];
	}
}

*/

