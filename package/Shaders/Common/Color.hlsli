#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"

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
	const static float AlbedoPreMult = 1 / AlbedoMult;                        // greater value -> brighter pbr
	const static float LightPreMult = 1 / (Math::PI * AlbedoPreMult);  // ensure 1/PI as product

	float3 GammaToLinear(float3 color)
	{
		return pow(abs(color), 2.2);
	}

	float3 LinearToGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 2.2);
	}

#if defined(LINEAR_LIGHTING)
	float3 Light(float3 color)
	{
#	if defined(TRUE_PBR) || (defined(SKIN) && defined(PBR_SKIN))
		return GammaToLinear(color) * AlbedoPreMult * Math::PI;
#	else
		return GammaToLinear(color) * AlbedoPreMult;
#	endif
	}
	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR)
		return color;
#	else
		return GammaToLinear(color) * AlbedoMult;
#	endif
	}
	float3 Tint(float3 color)
	{
		return GammaToLinear(color);
	}

	float3 Output(float3 color)
	{
#	if defined(DEFERRED)
		return color;
#	else
		return color;
		//return 0.22;
		//return LinearToGamma(color);
#	endif
	}
	float3 LLToGamma(float3 color)
	{
		return LinearToGamma(color);
	}
	float3 VanillaToPBR(float3 color){
		return GammaToLinear(color) * AlbedoMult;
	}

#else

	float3 Diffuse(float3 color)
	{
		return color;
	}
	float3 Tint(float3 color)
	{
		return color;
	}
	float3 Light(float3 color)
	{
#	if defined(TRUE_PBR)
		return GammaToLinear(color) * AlbedoPreMult * Math::PI;
#	else
		return color;
#	endif
	}
	float3 Output(float3 color)
	{
#	if defined(TRUE_PBR)
		return LinearToGamma(color);
#	else
		return color;
#	endif
	}
	float3 LLToGamma(float3 color)
	{
		return color;
	}
	float3 VanillaToPBR(float3 color){
		return color;
	}
#endif

}

#endif  //__COLOR_DEPENDENCY_HLSL__