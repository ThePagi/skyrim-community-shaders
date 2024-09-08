#include "Common/SharedData.hlsli"
#include "SnowCover/FastNoiseLite.hlsl"
#if defined(PSHADER)

Texture2D<float4> SnowDiffuse : register(t73);
Texture2D<float3> SnowNormal : register(t74);
Texture2D<float4> SnowRMAOS : register(t75);
Texture2D<float> SnowParallax : register(t76);

float MyHash11(float p)
{
	return frac(sin(p) * 1e4);
}

// https://blog.selfshadow.com/publications/blending-in-detail/
// for when s = (0,0,1)
float3 MyReorientNormal(float3 n1, float3 n2)
{
	n1 += float3(0, 0, 1);
	n2 *= float3(-1, -1, 1);

	return n1 * dot(n1, n2) / n1.z - n2;
}

// stolen from wetness effects
float SnowNoise(float3 pos)
{
	// https://github.com/BelmuTM/Noble/blob/master/LICENSE.txt

	const float3 step = float3(110.0, 241.0, 171.0);
	float3 i = floor(pos);
	float3 f = frac(pos);
	float n = dot(i, step);

	float3 u = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(lerp(MyHash11(n + dot(step, float3(0.0, 0.0, 0.0))), MyHash11(n + dot(step, float3(1.0, 0.0, 0.0))), u.x),
					lerp(MyHash11(n + dot(step, float3(0.0, 1.0, 0.0))), MyHash11(n + dot(step, float3(1.0, 1.0, 0.0))), u.x), u.y),
		lerp(lerp(MyHash11(n + dot(step, float3(0.0, 0.0, 1.0))), MyHash11(n + dot(step, float3(1.0, 0.0, 1.0))), u.x),
			lerp(MyHash11(n + dot(step, float3(0.0, 1.0, 1.0))), MyHash11(n + dot(step, float3(1.0, 1.0, 1.0))), u.x), u.y),
		u.z);
}

// http://chilliant.blogspot.com/2010/11/rgbhsv-in-hlsl.html
float3 Hue(float H)
{
	float R = abs(H * 6 - 3) - 1;
	float G = 2 - abs(H * 6 - 2);
	float B = 2 - abs(H * 6 - 4);
	return saturate(float3(R, G, B));
}

float3 HSVtoRGB(in float3 HSV)
{
	return ((Hue(HSV.x) - 1) * HSV.y + 1) * HSV.z;
}

float3 RGBtoHSV(in float3 RGB)
{
	float3 HSV = 0;
	HSV.z = max(RGB.r, max(RGB.g, RGB.b));
	float M = min(RGB.r, min(RGB.g, RGB.b));
	float C = HSV.z - M;

	if (C != 0) {
		HSV.y = C / HSV.z;
		float3 Delta = (HSV.z - RGB) / C;
		Delta.rgb -= Delta.brg;
		Delta.rg += float2(2, 4);
		if (RGB.r >= HSV.z)
			HSV.x = Delta.b;
		else if (RGB.g >= HSV.z)
			HSV.x = Delta.r;
		else
			HSV.x = Delta.g;
		HSV.x = frac(HSV.x / 6);
	}
	return HSV;
}

float GetHeightMult(float3 p)
{
	float height_tresh = p.z - snowCoverSettings.SnowHeightOffset - (p.x * 0.010569460362286 - p.y * 0.165389061732133 - p.x * p.x * 0.000000034552775 - p.x * p.y * 0.000000572526633 - p.y * p.y * 0.000000272913055 - p.x * p.x * p.x * 0.000000000001466 + p.x * p.x * p.y * 0.000000000000441 + p.x * p.y * p.y * 0.000000000003507 + p.y * p.y * p.y * 0.000000000006575);
	return height_tresh / 1000;
}

float GetEnvironmentalMultiplier(float3 p)
{
	return GetHeightMult(p);
}

void ApplySnowFoliage(inout float3 color, inout float3 worldNormal, inout float glossiness, inout float shininess, float3 p)
{
	fnl_state noise = fnlCreateState();
	noise.noise_type = FNL_NOISE_VALUE_CUBIC;
	float v = fnlGetNoise2D(noise, p.x * 512, p.y * 512);
	noise.octaves = 1;
	float mult = saturate(pow(abs(worldNormal.z), 0.5) - 0.25 * abs(v)) * saturate(GetEnvironmentalMultiplier(p));
	if (snowCoverSettings.AffectFoliageColor) {
		float gmult = saturate(GetHeightMult(p) - snowCoverSettings.FoliageHeightOffset / 1000);
		float3 hsv = RGBtoHSV(color);
		if (hsv.x > 0.5625)
			hsv.x = frac(lerp(hsv.x, 1.125, gmult));
		else
			hsv.x = lerp(hsv.x, 0.125, gmult);
		//hsv.z = pow(hsv.z, 1+gmult*0.5);
		color = HSVtoRGB(hsv);
	}
	//float mult = skylight;
	color = lerp(color, 0.35 + v * 0.05, mult);
	//color = worldNormal*0.5+0.5;
	glossiness = lerp(glossiness, 0.5 * pow(v, 3.0), mult);
	shininess = lerp(shininess, max(1, pow(1 - v, 3.0) * 100), mult);
	worldNormal = normalize(lerp(worldNormal, float3(0, 0, 1.0), mult));
}

float ApplySnowBase(inout float3 color, inout float3 worldNormal, inout float sh0, float snowDispScale, float3 p, float skylight, float3 viewPos, out float vnoise, out float snoise)
{
	float viewDist = max(1, sqrt(viewPos.z) / 512);  //max(1, (viewPos.z + (sin(viewPos.x * 7 + viewPos.z * 13))) / 512);
	fnl_state noise = fnlCreateState();
	noise.noise_type = FNL_NOISE_VALUE_CUBIC;
	noise.fractal_type = FNL_FRACTAL_PINGPONG;
	noise.ping_pong_strength = 1.0;
	noise.octaves = max(1, (2 / viewDist));
	float v = 0.5;  //fnlGetNoise2D(noise, p.x * 512, p.y * 512) / viewDist;
	noise.fractal_type = FNL_FRACTAL_FBM;
	noise.noise_type = FNL_NOISE_OPENSIMPLEX2S;
	noise.octaves = max(1, (5 / viewDist));
	float simplex_scale = 1;
	float s = 0.5;   //fnlGetNoise2D(noise, p.x * simplex_scale, p.y * simplex_scale) / viewDist;
	float sx = 0.5;  //fnlGetNoise2D(noise, p.x * simplex_scale + (1 + worldNormal.x)*viewDist, p.y * simplex_scale) / viewDist;
	float sy = 0.5;  //fnlGetNoise2D(noise, p.x * simplex_scale, p.y * simplex_scale + (1 + worldNormal.y)*viewDist) / viewDist;
	float mult = smoothstep(0, 1, saturate(pow(worldNormal.z, 2))) * skylight * smoothstep(0, 1, saturate(GetEnvironmentalMultiplier(p) + s + sh0 * snowDispScale));
	float parallax = 0.0001 * (SnowParallax.Sample(SampColorSampler, p.xy / 100).x - 0.5);
	if (!snowCoverSettings.AffectFoliageColor)
		parallax = 0;
	float2 uv = p.xy / 100 + parallax * viewPos.xy;
	color = lerp(color, SnowDiffuse.Sample(SampColorSampler, uv).rgb, mult);
	float3 normal = TransformNormal(SnowNormal.Sample(SampNormalSampler, uv).rgb);
	worldNormal = normalize(lerp(worldNormal, MyReorientNormal(worldNormal, normal), mult));
	sh0 = saturate(sh0 + mult * parallax);
	vnoise = (v)*0.5 + 0.5;
	snoise = s * 0.5 + 0.5;
	//color = normalize(abs(float3(sx - s, sy - s, 1.0-worldNormal.z)));
	//color = lerp(color, 0.35 + v * 0.05 + s * 0.001, mult);
	//color = 1/viewDist;
	//color = worldNormal*0.5+0.5;
	//worldNormal = normalize(lerp(worldNormal, normalize(float3(sx-s+sin(v*3.14)*0.02, sy-s+cos(v*3.14)*0.02,0.05+vnoise*0.05)), mult));
	//worldNormal = float3(0,0,1);
	//worldNormal = normalize(lerp(worldNormal, MyReorientNormal(worldNormal, normalize(float3(sx-s, sy-s,vnoise*0.2))), mult));
	return mult;
}
#	if defined(TRUE_PBR)
void ApplySnowPBR(inout float3 color, inout float3 worldNormal, inout PBR::SurfaceProperties prop, inout float sh0, float snowDispScale, float3 p, float skylight, float3 viewPos)
{
	float v;
	float s;
	float mult = ApplySnowBase(color, worldNormal, sh0, snowDispScale, p, skylight, viewPos, v, s);
	//color = lerp(color, 0.8 + s * 0.15, mult);
	prop.Metallic *= mult;
	prop.Roughness = lerp(prop.Roughness, 0.9 - 0.6 * pow(v * s, 3.0), mult);
	prop.F0 = lerp(prop.F0, 0.04, mult);
	//prop.AO = lerp(prop.AO, saturate(max(pow(0.5 * s, 0.5) + 0.5, v)), mult);
	prop.GlintScreenSpaceScale = lerp(prop.GlintScreenSpaceScale, snowCoverSettings.Glint.x, mult);
	prop.GlintLogMicrofacetDensity = lerp(prop.GlintLogMicrofacetDensity, snowCoverSettings.Glint.y, mult);
	prop.GlintMicrofacetRoughness = lerp(prop.GlintMicrofacetRoughness, snowCoverSettings.Glint.z, mult);
	prop.GlintDensityRandomization = lerp(prop.GlintDensityRandomization, snowCoverSettings.Glint.w, mult);
}
#	else
void ApplySnow(inout float3 color, inout float3 worldNormal, inout float glossiness, inout float shininess, inout float sh0, float snowDispScale, float3 p, float skylight, float3 viewPos)
{
	float v;
	float s;
	//color = sRGB2Lin(color);
	float mult = ApplySnowBase(color, worldNormal, sh0, snowDispScale, p, skylight, viewPos, v, s);
	//color = lerp(color, 0.35 + v * 0.05 + s * 0.001, mult);
	//color = Lin2sRGB(color);
	glossiness = lerp(glossiness, 0.5 * pow(v * s, 3.0), mult);
	shininess = lerp(shininess, max(1, pow(1 - v, 3.0) * 100), mult);
}
#	endif
#endif