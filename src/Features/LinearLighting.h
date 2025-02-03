#pragma once

#include "Feature.h"

class LinearLighting : public Feature
{
public:
	static LinearLighting* GetSingleton()
	{
		static LinearLighting singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Linear Lighting"; }
	virtual inline std::string GetShortName() override { return "LinearLighting"; }
	virtual inline std::string_view GetShaderDefineName() override { return "LINEAR_LIGHTING"; }
	virtual bool HasShaderDefine(RE::BSShader::Type) override { return true; }

	struct alignas(16) Settings
	{
		uint linearLighting = 1;
		float colorMatchingPow = 1.8;
		float colorMatchingMult = 1.25;
		uint pad0;
	};

	Settings settings;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
};
