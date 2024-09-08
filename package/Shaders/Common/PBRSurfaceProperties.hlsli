namespace PBR
{
	struct SurfaceProperties
	{
		float3 BaseColor;
		float Roughness;
		float Metallic;
		float AO;
		float3 F0;
		float3 SubsurfaceColor;
		float Thickness;
		float3 CoatColor;
		float CoatStrength;
		float CoatRoughness;
		float3 CoatF0;
		float3 FuzzColor;
		float FuzzWeight;
		float GlintScreenSpaceScale;
		float GlintLogMicrofacetDensity;
		float GlintMicrofacetRoughness;
		float GlintDensityRandomization;
	};

	SurfaceProperties InitSurfaceProperties()
	{
		SurfaceProperties surfaceProperties;

		surfaceProperties.Roughness = 1;
		surfaceProperties.Metallic = 0;
		surfaceProperties.AO = 1;
		surfaceProperties.F0 = 0.04;

		surfaceProperties.SubsurfaceColor = 0;
		surfaceProperties.Thickness = 0;

		surfaceProperties.CoatColor = 0;
		surfaceProperties.CoatStrength = 0;
		surfaceProperties.CoatRoughness = 0;
		surfaceProperties.CoatF0 = 0.04;

		surfaceProperties.FuzzColor = 0;
		surfaceProperties.FuzzWeight = 0;

		surfaceProperties.GlintScreenSpaceScale = 1.5;
		surfaceProperties.GlintLogMicrofacetDensity = 40.0;
		surfaceProperties.GlintMicrofacetRoughness = 0.015;
		surfaceProperties.GlintDensityRandomization = 2.0;

		return surfaceProperties;
	}
}