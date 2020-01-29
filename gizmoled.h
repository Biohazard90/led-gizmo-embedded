
#pragma once

#include <Arduino.h>

#include <colorutilities.h>

#define MAX_NUMBER_EFFECTS 24
#define NUM_AUDIO_POINTS 6
#define ANIMATION_DELAY int(1000/60) // FPS

enum EffectName
{
	EFFECTNAME_BLINK = 0,
	EFFECTNAME_WHEEL,
	EFFECTNAME_OPAQUE,
	EFFECTNAME_GRADIENT,
	EFFECTNAME_TEST,
	EFFECTNAME_VISUALIZER,
};

namespace GizmoLED
{
	enum VarType
	{
		VARTYPE_COLOR = 1,
		VARTYPE_SLIDER,
		VARTYPE_CHECKBOX,
	};

	enum VarName
	{
		VARNAME_COLOR = 0,
		VARNAME_COLOR1,
		VARNAME_COLOR2,
		VARNAME_COLOR3,
		VARNAME_NUMBEROFCOLORS,
		VARNAME_SPEED,
		VARNAME_BRIGHTNESS,
		VARNAME_LENGTH,
		VARNAME_DECAY,
		VARNAME_RAINBOWENABLED,
		VARNAME_RAINBOWSPEED,
		VARNAME_RAINBOWLENGTH,
		VARNAME_FADEIN,
		VARNAME_FADEOUT,
		VARNAME_ANGLE,
	};

	enum EffectType
	{
		EFFECTTYPE_DEFAULT = 0,
		EFFECTTYPE_VISUALIZER,
	};

	//struct Var
	//{
	//	VarType type;
	//	VarName name;
	//};

	typedef void(*FnEffectAnimation)(float frameTime);
	typedef void(*FnConnectionAnimation)(float frameTime, float percent);

	struct Effect
	{
		EffectType type;
		EffectName name;
		int eepOffset;

		uint8_t settingsSize;
		uint8_t *settings;
		uint8_t *defaultSettings;

		BLECharacteristic *characteristic;
		FnEffectAnimation fnEffectAnimation;
	};

	//void BeginEffect(FnEffectAnimation fnEffectAnimation);
	//void AddEffectVarColor(VarName name, uint8_t r, uint8_t g, uint8_t b);
	//void AddEffectVarSlider(VarName name, uint8_t value, uint8_t min, uint8_t max);
	//void AddEffectVarCheckbox(VarName name, bool enabled);
	//void EndEffect();
	
	extern GizmoLED::FnConnectionAnimation connectionAnimation;
	extern float audioData[NUM_AUDIO_POINTS];
	//extern bool *audioDecay;
}

//#define BEGIN_EFFECT_SETTINGS(name) \
//	uint8_t name ## Settings[] = {
//
//#define END_EFFECT_SETTINGS() \
//	};

#define BEGIN_EFFECT_SETTINGS(variableName, enumName, payload) \
	uint8_t variableName ## Data[] = { \
		payload \
	}; \
	namespace fx ## variableName { \
		static EffectName e = enumName;\
	} \
	namespace variableName ## Settings { \
		static uint8_t * base = variableName ## Data; \
		static uint8_t pos = 0;

#define END_EFFECT_SETTINGS() \
	}

#define DECLARE_EFFECT_SETTINGS_COLOR(name, r, g, b) \
	GizmoLED::VarType::VARTYPE_COLOR, name, r, g, b,

#define DECLARE_EFFECT_SETTINGS_SLIDER(name, value, min, max) \
	GizmoLED::VarType::VARTYPE_SLIDER, name, value, min, max,

#define DECLARE_EFFECT_SETTINGS_CHECKBOX(name, value) \
	GizmoLED::VarType::VARTYPE_CHECKBOX, name, value,

#define EFFECT_VAR_COLOR(varName) \
	uint8_t *varName = base + ((pos+=5) + 2 - 5);

#define EFFECT_VAR_SLIDER(varName) \
	uint8_t *varName = base + ((pos+=5) + 2 - 5);

#define EFFECT_VAR_CHECKBOX(varName) \
	uint8_t *varName = base + ((pos+=3) + 2 - 3);

//#define DECLARE_EFFECT_VAR(varName, effectName, offset) \
//	uint8_t *varName = effectName ## Settings + (offset + 2)

#define BEGIN_EFFECTS() \
	GizmoLED::Effect _effects[] = {

#define END_EFFECTS() \
	};

// (PROGMEM (uuid), BLERead | BLEWrite, sizeof name ## Settings),\

#define DECLARE_EFFECT(variableName, animationFunction, type) \
	{type, fx ## variableName::e, fx ## variableName::e + 1, \
	sizeof variableName ## Data, variableName ## Data, nullptr, \
	nullptr, \
	animationFunction},

#define GIZMOLED_SETUP() \
	{ \
		extern int numEffects; \
		numEffects = sizeof _effects / (sizeof _effects[0]); \
		String uuidBase(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa"); \
		for (int e; e < numEffects; ++e) { \
			GizmoLED::Effect &effect = _effects[e]; \
			String tempUuid = String(uuidBase + (e < 10 ? String("0") + String(e) : String(e))); \
			char *uuid = (char*)malloc(tempUuid.length() + 1); \
			const char *rd = tempUuid.c_str(); \
			char *wr = uuid; \
			while (*rd) { *wr = *rd; ++wr; ++rd; } \
			*wr = 0; \
			effect.characteristic = new BLECharacteristic( \
				uuid, \
				BLERead | BLEWrite, effect.settingsSize); \
		} \
		extern GizmoLED::Effect *effects; \
		effects = _effects; \
	} \
	extern void GizmoLEDSetup(); \
	GizmoLEDSetup()

#define GIZMOLED_LOOP() \
	extern void GizmoLEDLoop(); \
	GizmoLEDLoop()

//#define NUM_AUDIO_POINTS 6
//#define ANIMATION_DELAY int(1000/30) // FPS
//#define BLE_DELAY 0.25f
//#define BLE_DELAY_VISUALIZER 0.025f

//inline void copySmall(uint8_t *dst, uint8_t *src, int size) {
//	--size;
//	for (; size >= 0; --size) {
//		dst[size] = src[size];
//	}
//}
//
//#define GLED_IMPLEMENT_SERVICE() \
//	BLEService ledService(PROGMEM "e8942ca1-eef7-4a95-afeb-e3d07e8af52e"); \
//	int lastTime = 0; \
//	int animationDelayCompensation = 0; \
//	float frameTime = 0.0f; \
//	float bleUpdateTimer = 0; \
//	float bleCurrentUpdateDelay = BLE_DELAY; \
//	float connectionEffectTimer = 0.0f; \
//	BLEDevice central;

//#define GLED_IMPLEMENT_EFFECT_BLINK() \
//	uint8_t blinkSettings[] = { \
//		0xFF, 0, 0, \
//		10, \
//		0, \
//		1, \
//		0, \
//		10, \
//	}; \
//	uint8_t blinkSettingsDefaults[sizeof blinkSettings]; \
//	BLECharacteristic blinkSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa01", BLERead | BLEWrite, sizeof blinkSettings);
//
//#define GLED_EFFECT_BLINK_SETTINGS_COLOR (&blinkSettings[0])
//#define GLED_EFFECT_BLINK_SETTINGS_SPEED (blinkSettings[3])
//#define GLED_EFFECT_BLINK_SETTINGS_FADE_IN (blinkSettings[4])
//#define GLED_EFFECT_BLINK_SETTINGS_FADE_OUT (blinkSettings[5])
//#define GLED_EFFECT_BLINK_SETTINGS_RAINBOW_CYCLE_ENABLED (blinkSettings[6])
//#define GLED_EFFECT_BLINK_SETTINGS_RAINBOW_CYCLE_SPEED (blinkSettings[7])


//#define GLED_IMPLEMENT_VISUALIZER() \
//	uint8_t visualizerSettings[] = { \
//		0xFF, 0, 0, \
//		0xFF, 0x80, 0, \
//		255, \
//		255, \
//		0, \
//		0, \
//		10, \
//	}; \
//	uint8_t visualizerSettingsDefaults[sizeof visualizerSettings]; \
//	float audioData[NUM_AUDIO_POINTS] = { 0.0f }; \
//	bool audioDecay[NUM_AUDIO_POINTS] = { false }; \
//	BLECharacteristic visualizerSettingsCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-10cf850bfa04", BLERead | BLEWrite, sizeof visualizerSettings); \
//	BLECharacteristic audioDataCharacteristic(PROGMEM "e8942ca1-d9e7-4c45-b96c-20cf850bfa00", BLEWrite, sizeof audioData);
//
//#define GLED_EFFECT_VISUALIZER_SETTINGS_COLOR_0 (&visualizerSettings[0])
//#define GLED_EFFECT_VISUALIZER_SETTINGS_COLOR_1 (&visualizerSettings[3])
//#define GLED_EFFECT_VISUALIZER_SETTINGS_BRIGHTNESS (visualizerSettings[6])
//#define GLED_EFFECT_VISUALIZER_SETTINGS_DECAY (visualizerSettings[7])
//#define GLED_EFFECT_VISUALIZER_SETTINGS_SPEED (visualizerSettings[8])
//#define GLED_EFFECT_VISUALIZER_SETTINGS_RAINBOW_CYCLE_ENABLED (visualizerSettings[9])
//#define GLED_EFFECT_VISUALIZER_SETTINGS_RAINBOW_CYCLE_SPEED (visualizerSettings[10])
//
//#define GLED_IMPLEMENT_FUNCTIONS() \
//	uint8_t functionCallState[] = {  \
//		0, \
//		0, \
//	};
