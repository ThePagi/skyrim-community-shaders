#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "State.h"

struct SnowCover : Feature
{
public:
	static SnowCover* GetSingleton()
	{
		static SnowCover singleton;
		return &singleton;
	}

	virtual inline std::string GetName() { return "Snow Cover"; }
	virtual inline std::string GetShortName() { return "SnowCover"; }
	inline std::string_view GetShaderDefineName() override { return "SNOW_COVER"; }

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		uint EnableSnowCover = true;

	};

	struct alignas(16) PerFrame
	{
		uint Month;
		float Time;
		float Snowing;
		float SnowAmount;
		float SnowpileAmount;
		Settings settings;

		float pad[2];
	};

	Settings settings;

	PerFrame GetCommonBufferData();

	bool requiresUpdate = true;
	float wetnessDepth = 0.0f;
	float puddleDepth = 0.0f;
	float lastGameTimeValue = 0.0f;
	uint32_t currentWeatherID = 0;
	uint32_t lastWeatherID = 0;
	float previousWeatherTransitionPercentage = 0.0f;

	virtual void SetupResources();
	virtual void Reset();

	virtual void DrawSettings();

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);

	virtual void RestoreDefaultSettings();
	float CalculateWeatherTransitionPercentage(float skyCurrentWeatherPct, float beginFade, bool fadeIn);
	void CalculateWetness(RE::TESWeather* weather, RE::Sky* sky, float seconds, float& wetness, float& puddleWetness);

	virtual inline void PostPostLoad() override { Hooks::Install(); }

	struct Hooks
	{
		static void Install()
		{
		}
	};

	bool SupportsVR() override { return true; };
};
