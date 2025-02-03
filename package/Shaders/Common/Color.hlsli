#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

namespace Color
{
	static float GammaCorrectionValue = 2.2;

	float RGBToLuminance(float3 color)
	{
		return dot(color, float3(0.2125, 0.7154, 0.0721));
	}

	float RGBToLuminanceAlternative(float3 color)
	{
		return dot(color, float3(0.3, 0.59, 0.11));
	}

	float RGBToLuminance2(float3 color)
	{
		return dot(color, float3(0.299, 0.587, 0.114));
	}

	float3 RGBToYCoCg(float3 color)
	{
		float tmp = 0.25 * (color.r + color.b);
		return float3(
			tmp + 0.5 * color.g,        // Y
			0.5 * (color.r - color.b),  // Co
			-tmp + 0.5 * color.g        // Cg
		);
	}

	float3 YCoCgToRGB(float3 color)
	{
		float tmp = color.x - color.z;
		return float3(
			tmp + color.y,
			color.x + color.z,
			tmp - color.y);
	}

	const static float AlbedoMult = 1.7;
	const static float AlbedoPreMult = 1 / AlbedoMult;                 // greater value -> brighter pbr
	const static float LightPreMult = 1 / (Math::PI * AlbedoPreMult);  // ensure 1/PI as product

	float3 GammaToLinear(float3 color)
	{
		return pow(abs(color), 2.2);
	}

	float3 LinearToGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 2.2);
	}

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
#if defined(LINEAR_LIGHTING)
	float3 Light(float3 color)
	{
#	if defined(TRUE_PBR) || (defined(SKIN) && defined(PBR_SKIN))
		return GammaToLinear(color) * Math::PI;
#	else
		return GammaToLinear(color);
#	endif
	}
	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR)
		return color;
#	else
		return pow(color, SharedData::extendedMaterialSettings.ColorMatchingPow) * SharedData::extendedMaterialSettings.ColorMatchingMult;
#	endif
	}
	float3 Tint(float3 color)
	{
		return GammaToLinear(color);
	}
	float3 Ambient(float3 color)
	{
		return GammaToLinear(color);
	}
	float3 Output(float3 color)
	{
#	if defined(DEFERRED)
		return color;
#	else
		return color;
#	endif
	}
	float3 LLToGamma(float3 color)
	{
		return LinearToGamma(color);
	}
	float3 VanillaToPBR(float3 color){
		return pow(color, SharedData::extendedMaterialSettings.ColorMatchingPow) * SharedData::extendedMaterialSettings.ColorMatchingMult;
	}

#else

	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR) || (defined(SKIN) && defined(PBR_SKIN))
		return pow(color / SharedData::extendedMaterialSettings.ColorMatchingMult, 1./SharedData::extendedMaterialSettings.ColorMatchingPow);
#	else
		return color;
#	endif
	}
	float3 Tint(float3 color)
	{
		return color;
	}
	float3 Light(float3 color)
	{
#	if defined(TRUE_PBR) || (defined(SKIN) && defined(PBR_SKIN))
		return color * Math::PI;
#	else
		return color;
#	endif
	}
	float3 Ambient(float3 color)
	{
		return color;
	}
	float3 Output(float3 color)
	{
		return color;
	}
	float3 LLToGamma(float3 color)
	{
		return color;
	}
	float3 VanillaToPBR(float3 color)
	{
		return color;
	}
#endif
#endif
}

#endif  //__COLOR_DEPENDENCY_HLSL__