
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
	EFFECTNAME_PULSE,
	EFFECTNAME_SPARKLE,
	EFFECTNAME_CHRISTMAS,
	EFFECTNAME_ACCELERATION,
	EFFECTNAME_NOISELEVEL,
	EFFECTNAME_EMPTY,
	EFFECTNAME_WAVES,
	EFFECTNAME_DROPS,
	EFFECTNAME_METEOR,
	EFFECTNAME_WIPE,
	EFFECTNAME_FIRE,
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
		VARNAME_SENSITIVITY,
		VARNAME_COLORBACKGROUND,
		VARNAME_RAINBOWOFFSET,
		VARNAME_BACKGROUNDBRIGHTNESS,
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
	typedef void(*FnEffectChangedCallback)(int newEffectType, int lastEffectType);

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

	extern GizmoLED::FnConnectionAnimation connectionAnimation;
	extern GizmoLED::FnEffectChangedCallback effectChangedCallback;
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
		static uint8_t *base = variableName ## Data; \
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

#define BEGIN_EFFECTS() \
	GizmoLED::Effect _effects[] = {

#define END_EFFECTS() \
	};

// (PROGMEM (uuid), BLERead | BLEWrite, sizeof name ## Settings),\

#define DECLARE_EFFECT(variableName, animationFunction, type) \
	{type, fx ## variableName::e, fx ## variableName::e + 2, \
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
