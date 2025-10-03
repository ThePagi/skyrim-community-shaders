#ifndef __MATH_DEPENDENCY_HLSL__
#define __MATH_DEPENDENCY_HLSL__

#define EPSILON_SSS_ALBEDO 1e-3f  // For albedo clamping in SSS calculations
#define EPSILON_DOT_CLAMP 1e-5f  // For dot product clamping
#define EPSILON_DIVISION  1e-6f	 // For division to avoid division by zero

namespace Math
{
	static const float4x4 IdentityMatrix = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};

	static const float PI = 3.1415926535897932384626433832795f;  // PI
	static const float HALF_PI = PI * 0.5f;                      // PI / 2
	static const float TAU = PI * 2.0f;                          // PI * 2

	float2 pixel(Texture2D<float4> tex, float2 uv){
		float scale = 256;
		float rx;
		float ry;
		tex.GetDimensions(rx, ry);
		uv = frac(uv);
		return floor(float2(uv.x*rx/ry, uv.y)*scale)/(scale);
	}

	float2 pixel_world(Texture2D<float4> tex, float2 uv, float3 pos){
		return pixel(tex, uv);
		uv = frac(uv);
		float fdx = length(ddx(pos))/length(ddx(uv));
		float fdy = length(ddy(pos))/length(ddy(uv));
		float scale = 0.5*fdx;
		float rx;
		float ry;
		tex.GetDimensions(rx, ry);
		return floor(float2(uv.x*rx/ry, uv.y)*scale)/(scale);
	}


}

#endif  //__MATH_DEPENDENCY_HLSL__