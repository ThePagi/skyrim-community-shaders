#include "LinearLighting.h"

#include "Hooks.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LinearLighting::Settings,
	linearLighting,
	colorMatchingPow,
	colorMatchingMult);

void LinearLighting::DrawSettings()
{
	ImGui::Checkbox("Enable Linear Lighting", (bool*)&settings.linearLighting);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Enabled: lighting in linear, vanilla diffuse texture converted to linear.\nDisabled: lighting in gamma space, PBR albedo converted to gamma.");
	}
	ImGui::SliderFloat("Color Matching Power", &settings.colorMatchingPow, 1.0f, 2.2f);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Power applied when converting vanilla to linear or pbr to vanilla. Standard between sRGB and linear is 2.2");
	}
	ImGui::SliderFloat("Color Matching Multiplier", &settings.colorMatchingMult, 1.0, 2.0, "%.1f");
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Multiplier applied after conversion");
	}
}

void LinearLighting::SaveSettings(json& o_json)
{
	o_json = settings;
}

void LinearLighting::LoadSettings(json& o_json)
{
	settings = o_json;
}

void LinearLighting::RestoreDefaultSettings()
{
	settings = {};
}
