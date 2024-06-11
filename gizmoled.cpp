
#include <ArduinoBLE.h>

#include "gizmoled.h"

//#include <Wire.h>
//#include <extEEPROM.h>

#define MAX_DEVICE_NAME 32

#ifdef ARDUINO_ARCH_NRF52840
#include "blesenseflash.h"
#define FLASH_DEVICE_NAME_OFFSET 1
#define FLASH_EFFECT_BASE_OFFSET 2
#elif ESP32
#include <EEPROM.h>
#endif

using namespace GizmoLED;

//#define LED_PIN 2
//#define NUM_LEDS 101

#define BLE_DELAY 0.1f
#define BLE_DELAY_VISUALIZER 0.0f
#define EEP_ROM_PAGE_SIZE 128
#define CONNECTION_FX_TIME 1.5f
#define AUDIO_HOLD_TIME 10.0f

// Persisted data
#define GENERIC_INIT_MAGIC 0x47
struct Generic
{
	uint8_t selectedEffect = 0;
	uint8_t selectedEffectSecondary = 0;
	uint8_t numberOfEffects = 0;
	uint8_t effectNames[MAX_NUMBER_EFFECTS] = { 0 };
	uint8_t isInitialized = 0;
};

Generic genericData;
char deviceName[MAX_DEVICE_NAME];


#ifdef ARDUINO_ARCH_NRF52840
constexpr int flashBlockSize = BLEFLASH_BLOCK_SIZE(EEP_ROM_PAGE_SIZE * (2 + MAX_NUMBER_EFFECTS));
BLEFLASH_DECLARE_VARIABLE(flashAll, flashBlockSize) = {};
BLEFLASH_DECLARE_ACCESS(uint8_t, flashGeneric, flashAll, 0);
BLEFLASH_DECLARE_ACCESS(uint8_t, flashDeviceName, flashAll, FLASH_DEVICE_NAME_OFFSET * EEP_ROM_PAGE_SIZE);
BLEFLASH_DECLARE_ACCESS(uint8_t, flashEffectBase, flashAll, FLASH_EFFECT_BASE_OFFSET * EEP_ROM_PAGE_SIZE);
#elif ESP32
#endif

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
		//Serial.println("Animating effect index");
		//Serial.println(genericData.selectedEffect);
		
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

int GetTotalEEPROMSize()
{
	int totalSize = sizeof(genericData) + sizeof(deviceName);
	for (int i = 0; i < numEffects; ++i)
	{
		Effect &effect = effects[i];
		totalSize += effect.settingsSize;
	}
	return totalSize;
}

void StoreCurrentSettings()
{
	//Serial.println("Flashing settings...");

#ifdef ARDUINO_ARCH_NRF52840
	uint8_t *data = (uint8_t*)malloc(flashBlockSize);

	genericData.isInitialized = GENERIC_INIT_MAGIC;
	copySmall(data, (uint8_t*)&genericData, sizeof(struct Generic));
	copySmall(data + FLASH_DEVICE_NAME_OFFSET * EEP_ROM_PAGE_SIZE, (uint8_t*)deviceName, MAX_DEVICE_NAME);
	for (int i = 0; i < numEffects; ++i)
	{
		Effect &effect = effects[i];
		copySmall(data + (FLASH_EFFECT_BASE_OFFSET + i) * EEP_ROM_PAGE_SIZE, effect.settings, effect.settingsSize);
	}
	
	BLEFLASH_WRITE(flashAll, flashBlockSize, data);

	free(data);
#elif ESP32
	int totalSize = GetTotalEEPROMSize();
	EEPROM.begin(totalSize);

	genericData.isInitialized = GENERIC_INIT_MAGIC;
	EEPROM.put(0, genericData);
	int writePos = sizeof(genericData);
	EEPROM.put(writePos, deviceName);
	writePos += sizeof(deviceName);
	for (int i = 0; i < numEffects; ++i)
	{
		Effect &effect = effects[i];
		//copySmall(data + (FLASH_EFFECT_BASE_OFFSET + i) * EEP_ROM_PAGE_SIZE, effect.settings, effect.settingsSize);
		for (int b = 0; b < effect.settingsSize; ++b)
		{
			EEPROM.put(writePos++, *(effect.settings + b));
		}
	}
	
	EEPROM.end();
#endif
}

void GizmoLEDSetup()
{
	Serial.begin(115200);
	Serial.setTimeout(50);


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
#ifdef ARDUINO_ARCH_NRF52840
	if (((Generic*)flashGeneric)->isInitialized == GENERIC_INIT_MAGIC)
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
	}
#elif ESP32
	int totalSize = GetTotalEEPROMSize();
	Serial.println("Init data size:");
	Serial.println(totalSize);

	EEPROM.begin(totalSize);
	EEPROM.get(0, genericData);
	if (genericData.isInitialized == GENERIC_INIT_MAGIC)
	{
		Serial.println("Init from EEPROM");
		int readPos = sizeof(genericData);
		EEPROM.get(readPos, deviceName);
		readPos += sizeof(deviceName);
		
		Serial.println(deviceName);
		
		for (int i = 0; i < numEffects; ++i)
		{
			Effect &effect = effects[i];
			//uint8_t *flashEffectSettings = flashEffectBase + EEP_ROM_PAGE_SIZE * i;
			//copySmall(effect.settings, flashEffectSettings, effect.settingsSize);
			for (int b = 0; b < effect.settingsSize; ++b)
			{
				*(effect.settings + b) = EEPROM.read(readPos++);
			}
		}
	}
	EEPROM.end();
	Serial.println("EEPROM Init ended");
#endif
	//else
	//{
		// If isInitialized isn't set, we need to initialize all settings based on their default settings because the flash is empty
		//Serial.println("Init defaults");
	//}

	if (*deviceName != 0)
	{
		BLE.setLocalName((const char*)deviceName);
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

	Serial.println("Continue Init 1");
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
	Serial.println("Continue Init 2");
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
	
	Serial.println("Continue Init 3");

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

	Serial.println("Continue Init 4");
	
	// Advertise
	BLE.addService(ledService);
	//BLE.setAdvertisedService(ledService);

	SetVisualizerInputSupported(genericData.selectedEffect < numEffects &&
		effects[genericData.selectedEffect].type == EFFECTTYPE_VISUALIZER);

	BLE.setAdvertisingInterval(320); // 160 == 100ms
	BLE.advertise();

	Serial.println("Continue Init 5");
	BLE.setEventHandler(BLEConnected, blePeripheralConnectedHandler);
	//BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectedHandler);
	
	if (effectChangedCallback != nullptr)
	{
		effectChangedCallback(effects[genericData.selectedEffect].name, -1);
	}
	
	Serial.println("Continue Init 6");
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

