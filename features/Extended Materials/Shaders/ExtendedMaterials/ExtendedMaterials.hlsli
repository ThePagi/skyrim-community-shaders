// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

struct DisplacementParams
{
	float DisplacementScale;
	float DisplacementOffset;
	float HeightScale;
};

namespace ExtendedMaterials
{
	float DisplacementHeight0(float displacement, DisplacementParams params)
	{
		return (displacement - 0.5) * params.HeightScale + params.DisplacementOffset;
	}
	float Displacement01(float displacement, DisplacementParams params)
	{
		return (displacement - 0.5) * params.DisplacementScale + params.DisplacementScale/2 + params.DisplacementOffset;
	}

	float4 Displacement01(float4 displacement, DisplacementParams params)
	{
		return float4(Displacement01(displacement.x, params), Displacement01(displacement.y, params), Displacement01(displacement.z, params), Displacement01(displacement.w, params));
	}

	float GetMipLevel(float2 coords, Texture2D<float4> tex)
	{
		// Compute the current gradients:
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);

#if !defined(PARALLAX) && !defined(TRUE_PBR)
		textureDims /= 2.0;
#endif

		float2 texCoordsPerSize = coords * textureDims;

		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);

		// Find min of change in u and v across quad: compute du and dv magnitude across quad
		float2 dTexCoords = dxSize * dxSize + dySize * dySize;

		// Standard mipmapping uses max here
		float minTexCoordDelta = max(dTexCoords.x, dTexCoords.y);

		// Compute the current mip level  (* 0.5 is effectively computing a square root before )
		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

#if !defined(PARALLAX) && !defined(TRUE_PBR)
		mipLevel++;
#endif

		return mipLevel;
	}

#if defined(LANDSCAPE)
#	if defined(TRUE_PBR)
#	define WEIGHT_POWER (0)
#	define HEIGHT_POWER 20
	float GetTerrainHeight(PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2, out float weights[6])
	{
		float weightBlend = 1 + blendFactor * WEIGHT_POWER;
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		weights[0] = w1.x;
		weights[1] = w1.y;
		weights[2] = w1.z;
		weights[3] = w1.w;
		weights[4] = w2.x;
		weights[5] = w2.y;
		float total = 0;
		[branch] if ((PBRFlags & TruePBR_LandTile0HasDisplacement) != 0 && w1.x > 0.0){
			float h = DisplacementHeight0(TexLandDisplacement0Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).x, params[0]);
			total += h * weights[0];
			weights[0] *= pow(heightBlend, h);
		}
		[branch] if ((PBRFlags & TruePBR_LandTile1HasDisplacement) != 0 && w1.y > 0.0){
			float h = DisplacementHeight0(TexLandDisplacement1Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).x, params[1]);
			total += h * weights[1];
			weights[1] *= pow(heightBlend, h);
		}
		[branch] if ((PBRFlags & TruePBR_LandTile2HasDisplacement) != 0 && w1.z > 0.0){
			float h = DisplacementHeight0(TexLandDisplacement2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).x, params[2]);
			total += h * weights[2];
			weights[2] *= pow(heightBlend, h);
		}
		[branch] if ((PBRFlags & TruePBR_LandTile3HasDisplacement) != 0 && w1.w > 0.0){
			float h = DisplacementHeight0(TexLandDisplacement3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).x, params[3]);
			total += h * weights[3];
			weights[3] *= pow(heightBlend, h);
		}
		[branch] if ((PBRFlags & TruePBR_LandTile4HasDisplacement) != 0 && w2.x > 0.0){
			float h = DisplacementHeight0(TexLandDisplacement4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).x, params[4]);
			total += h * weights[4];
			weights[4] *= pow(heightBlend, h);
		}
		[branch] if ((PBRFlags & TruePBR_LandTile5HasDisplacement) != 0 && w2.y > 0.0){
			float h = DisplacementHeight0(TexLandDisplacement5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).x, params[5]);
			total += h * weights[5];
			weights[5] *= pow(heightBlend, h);
		}
		float wsum = 0;
		[unroll] for (int i = 0; i < 6; i++)
		{
			wsum += weights[i];
		}
		float invwsum = rcp(wsum);
		[unroll] for (int i = 0; i < 6; i++)
		{
			weights[i] *= invwsum;
		}
		return total;
	}
#	else
#	define HEIGHT_POWER 4.0
#	define INV_HEIGHT_POWER 0.25
	float GetTerrainHeight(PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, out float pixelOffset[6])
	{
		float4 w1 = pow(input.LandBlendWeights1, 1 + 1 * blendFactor);
		float2 w2 = pow(input.LandBlendWeights2.xy, 1 + 1 * blendFactor);
		float blendPower = blendFactor * HEIGHT_POWER;
		// important to zero initialize, otherwise invalid/old values will be used here and as weights in Lighting.hlsl
		pixelOffset[0] = w1.x;
		pixelOffset[1] = w1.y;
		pixelOffset[2] = w1.z;
		pixelOffset[3] = w1.w;
		pixelOffset[4] = w2.x;
		pixelOffset[5] = w2.y;
		if (w1.x > 0.0)
			pixelOffset[0] *= 0.001 + pow(TexColorSampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).w, blendPower);
		if (w1.y > 0.0)
			pixelOffset[1] *= 0.001 + pow(TexLandColor2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).w, blendPower);
		if (w1.z > 0.0)
			pixelOffset[2] *= 0.001 + pow(TexLandColor3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).w, blendPower);
		if (w1.w > 0.0)
			pixelOffset[3] *= 0.001 + pow(TexLandColor4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).w, blendPower);
		if (w2.x > 0.0)
			pixelOffset[4] *= 0.001 + pow(TexLandColor5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).w, blendPower);
		if (w2.y > 0.0)
			pixelOffset[5] *= 0.001 + pow(TexLandColor6Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).w, blendPower);

		float total = 0;
		[unroll] for (int i = 0; i < 6; i++)
		{
			total += pixelOffset[i];
		}
		float invtotal = rcp(total);
		[unroll] for (int i = 0; i < 6; i++)
		{
			pixelOffset[i] *= invtotal;
		}
		return pow(total, rcp(blendPower));
	}
#	endif

#endif

#if defined(LANDSCAPE)
	float2 GetParallaxCoords(PS_INPUT input, float distance, float2 coords, float mipLevels[6], float3 viewDir, float3x3 tbn, float noise, DisplacementParams params[6], out float pixelOffset, out float weights[6])
#else
	float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset)
#endif
	{
		float3 viewDirTS = normalize(mul(tbn, viewDir));
		viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3;  // Fix for objects at extreme viewing angles

		float nearBlendToFar = saturate(distance / 2048.0);
#if defined(LANDSCAPE)
	// When CPM flag is disabled, will use linear blending as before.
	#if defined(TRUE_PBR)
		float blendFactor = extendedMaterialSettings.EnableComplexMaterial ? sqrt(saturate(1 - nearBlendToFar)) : 0;
		float4 w1 = lerp(input.LandBlendWeights1, smoothstep(0,1, input.LandBlendWeights1), blendFactor);
		float2 w2 = lerp(input.LandBlendWeights2.xy, smoothstep(0,1, input.LandBlendWeights2.xy), blendFactor);
		float scale = max(params[0].HeightScale*w1.x, max(params[1].HeightScale*w1.y, max(params[2].HeightScale*w1.z, max(params[3].HeightScale*w1.w, max(params[4].HeightScale*w2.x, params[5].HeightScale*w2.y)))));
		float maxHeight = 0.1 * scale;
	#else
		float blendFactor = extendedMaterialSettings.EnableComplexMaterial ? saturate(1 - nearBlendToFar) : INV_HEIGHT_POWER;
		float scale = 1;
		float maxHeight = 0.1 * scale;
	#endif
#else
		float scale = params.HeightScale;
		float maxHeight = 0.1 * scale;
#endif
		float minHeight = maxHeight * 0.5;

		if (nearBlendToFar < 1.0) {
#if defined(LANDSCAPE)
			uint numSteps = uint((max(4, scale*32) * (1.0 - nearBlendToFar)) + 0.5);
			numSteps = clamp((numSteps + 3) & ~0x03, 4, max(4, scale*32));
#else
			uint numSteps = uint((32 * (1.0 - nearBlendToFar)) + 0.5);
			numSteps = clamp((numSteps + 3) & ~0x03, 4, 32);
#endif

			float stepSize = rcp(numSteps);

			float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
			float2 prevOffset = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

			float prevBound = 1.0;
			float prevHeight = 1.0;

			float2 pt1 = 0;
			float2 pt2 = 0;

			[loop] while (numSteps > 0)
			{
				float4 currentOffset[2];
				currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
				currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;
				float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

				float4 currHeight;
#if defined(LANDSCAPE)
	#if defined(TRUE_PBR)
				currHeight.x = GetTerrainHeight(input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, weights) / scale + 0.5;
				currHeight.y = GetTerrainHeight(input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, weights) / scale + 0.5;
				currHeight.z = GetTerrainHeight(input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, weights) / scale + 0.5;
				currHeight.w = GetTerrainHeight(input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, weights) / scale + 0.5;
	#else
				currHeight.x = GetTerrainHeight(input, currentOffset[0].xy, mipLevels, params, blendFactor, weights);
				currHeight.y = GetTerrainHeight(input, currentOffset[0].zw, mipLevels, params, blendFactor, weights);
				currHeight.z = GetTerrainHeight(input, currentOffset[1].xy, mipLevels, params, blendFactor, weights);
				currHeight.w = GetTerrainHeight(input, currentOffset[1].zw, mipLevels, params, blendFactor, weights);
	#endif	
#else
				currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, mipLevel)[channel];
				currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, mipLevel)[channel];
				currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, mipLevel)[channel];
				currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, mipLevel)[channel];

				currHeight = Displacement01(currHeight, params);
#endif

				bool4 testResult = currHeight >= currentBound;
				[branch] if (any(testResult))
				{
					[flatten] if (testResult.w)
					{
						pt1 = float2(currentBound.w, currHeight.w);
						pt2 = float2(currentBound.z, currHeight.z);
					}
					[flatten] if (testResult.z)
					{
						pt1 = float2(currentBound.z, currHeight.z);
						pt2 = float2(currentBound.y, currHeight.y);
					}
					[flatten] if (testResult.y)
					{
						pt1 = float2(currentBound.y, currHeight.y);
						pt2 = float2(currentBound.x, currHeight.x);
					}
					[flatten] if (testResult.x)
					{
						pt1 = float2(currentBound.x, currHeight.x);
						pt2 = float2(prevBound, prevHeight);
					}
					break;
				}

				prevOffset = currentOffset[1].zw;
				prevBound = currentBound.w;
				prevHeight = currHeight.w;
				numSteps -= 4;
			}

			float delta2 = pt2.x - pt2.y;
			float delta1 = pt1.x - pt1.y;
			float denominator = delta2 - delta1;

			float parallaxAmount = 0.0;
			[flatten] if (denominator == 0.0)
			{
				parallaxAmount = 0.0;
			}
			else
			{
				parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;
			}

			nearBlendToFar *= nearBlendToFar;

			float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
			pixelOffset = lerp(parallaxAmount*scale, 0, nearBlendToFar);
			return lerp(viewDirTS.xy * offset + coords.xy, coords, nearBlendToFar);
		}

#if defined(LANDSCAPE)
		weights[0] = input.LandBlendWeights1.x;
		weights[1] = input.LandBlendWeights1.y;
		weights[2] = input.LandBlendWeights1.z;
		weights[3] = input.LandBlendWeights1.w;
		weights[4] = input.LandBlendWeights2.x;
		weights[5] = input.LandBlendWeights2.y;
#endif

		pixelOffset = 0;
		return coords;
	}

	// https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf

	// Cheap method of creating shadows using height for a given light source
	float GetParallaxSoftShadowMultiplier(float2 coords, float mipLevel, float3 L, float sh0, Texture2D<float4> tex, SamplerState texSampler, uint channel, float quality, float noise, DisplacementParams params)
	{
		[branch] if (quality > 0.0)
		{
			float2 rayDir = L.xy * 0.1;
			float4 multipliers = rcp((float4(4, 3, 2, 1) + noise));
			float4 sh;
			sh.x = Displacement01(tex.SampleLevel(texSampler, coords + rayDir * multipliers.x, mipLevel)[channel], params);
			sh.y = Displacement01(tex.SampleLevel(texSampler, coords + rayDir * multipliers.y, mipLevel)[channel], params);
			sh.z = Displacement01(tex.SampleLevel(texSampler, coords + rayDir * multipliers.z, mipLevel)[channel], params);
			sh.w = Displacement01(tex.SampleLevel(texSampler, coords + rayDir * multipliers.w, mipLevel)[channel], params);
			return 1.0 - saturate(dot(max(0, sh - sh0), 1.0) * params.HeightScale * 2.0) * quality;
		}
		return 1.0;
	}

#if defined(LANDSCAPE)
	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6])
	{
		if (quality > 0.0) {
			float2 rayDir = L.xy * 0.1;
			float4 multipliers = rcp((float4(4, 3, 2, 1) + noise));
			float4 sh;
			float heights[6] = { 0, 0, 0, 0, 0, 0 };
	#if defined(TRUE_PBR)
			sh.x = GetTerrainHeight(input, coords + rayDir * multipliers.x, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2, heights);
			sh.y = GetTerrainHeight(input, coords + rayDir * multipliers.y, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2, heights);
			sh.z = GetTerrainHeight(input, coords + rayDir * multipliers.z, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2, heights);
			sh.w = GetTerrainHeight(input, coords + rayDir * multipliers.w, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2, heights);
	#else
			sh.x = GetTerrainHeight(input, coords + rayDir * multipliers.x, mipLevel, params, quality, heights);
			sh.y = GetTerrainHeight(input, coords + rayDir * multipliers.y, mipLevel, params, quality, heights);
			sh.z = GetTerrainHeight(input, coords + rayDir * multipliers.z, mipLevel, params, quality, heights);
			sh.w = GetTerrainHeight(input, coords + rayDir * multipliers.w, mipLevel, params, quality, heights);
	#endif
			return 1.0 - saturate(dot(max(0, sh - sh0), 1.0) * 2.0) * quality;
		}
		return 1.0;
	}
#endif
}
