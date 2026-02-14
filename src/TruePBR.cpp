#include "TruePBR.h"

#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include "TruePBR/BSLightingShaderMaterialPBRLandscape.h"

#include "Features/InteriorSun.h"
#include "Hooks.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	GlintParameters,
	enabled,
	screenSpaceScale,
	logMicrofacetDensity,
	microfacetRoughness,
	densityRandomization);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TruePBR::PBRTextureSetData,
	roughnessScale,
	displacementScale,
	specularLevel,
	subsurfaceColor,
	subsurfaceOpacity,
	coatColor,
	coatStrength,
	coatRoughness,
	coatSpecularLevel,
	innerLayerDisplacementOffset,
	fuzzColor,
	fuzzWeight,
	glintParameters);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TruePBR::PBRMaterialObjectData,
	baseColorScale,
	roughness,
	specularLevel,
	glintParameters);

#define CHECK_PBR_TEXTURE(textureName)                                                                         \
	if (!(pbrMaterial->textureName)) {                                                                         \
		logger::warn("[TruePBR] {} missing {}; treating as nonPBR", pbrMaterial->inputFilePath, #textureName); \
		return false;                                                                                          \
	}

namespace PNState
{
	void ReadPBRRecordConfigs(const std::string& rootPath, std::function<void(const std::string&, const json&)> recordReader)
	{
		if (std::filesystem::exists(rootPath)) {
			auto configs = clib_util::distribution::get_configs(rootPath, "", ".json");

			if (configs.empty()) {
				logger::warn("[TruePBR] no .json files were found within the {} folder, aborting...", rootPath);
				return;
			}

			logger::info("[TruePBR] {} matching jsons found", configs.size());

			for (auto& path : configs) {
				logger::info("[TruePBR] loading json : {}", path);

				std::ifstream fileStream(path);
				if (!fileStream.is_open()) {
					logger::error("[TruePBR] failed to read {}", path);
					continue;
				}

				json config;
				try {
					fileStream >> config;
				} catch (const nlohmann::json::parse_error& e) {
					logger::error("[TruePBR] failed to parse {} : {}", path, e.what());
					continue;
				}

				const auto editorId = std::filesystem::path(path).stem().string();
				recordReader(editorId, config);
			}
		}
	}

	void SavePBRRecordConfig(const std::string& rootPath, const std::string& editorId, const json& config)
	{
		std::filesystem::create_directory(rootPath);

		const std::string outputPath = std::format("{}\\{}.json", rootPath, editorId);
		std::ofstream fileStream(outputPath);
		if (!fileStream.is_open()) {
			logger::error("[TruePBR] failed to write {}", outputPath);
			return;
		}
		try {
			fileStream << std::setw(4) << config;
		} catch (const nlohmann::json::type_error& e) {
			logger::error("[TruePBR] failed to serialize {} : {}", outputPath, e.what());
			return;
		}
	}
}

void SetupPBRLandscapeTextureParameters(BSLightingShaderMaterialPBRLandscape& material, const TruePBR::PBRTextureSetData& textureSetData, uint32_t textureIndex);

void TruePBR::DrawSettings()
{
	if (ImGui::CollapsingHeader("PBR", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		if (ImGui::TreeNodeEx("Texture Set Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (Util::SearchableCombo("Texture Set", selectedPbrTextureSetName, pbrTextureSets)) {
				selectedPbrTextureSet = &pbrTextureSets[selectedPbrTextureSetName];
			}

			if (selectedPbrTextureSet != nullptr) {
				bool wasEdited = false;
				if (ImGui::SliderFloat("Displacement Scale", &selectedPbrTextureSet->displacementScale, 0.f, 3.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Roughness Scale", &selectedPbrTextureSet->roughnessScale, 0.f, 3.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Specular Level", &selectedPbrTextureSet->specularLevel, 0.f, 3.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::TreeNodeEx("Subsurface")) {
					if (ImGui::ColorPicker3("Subsurface Color", &selectedPbrTextureSet->subsurfaceColor.red)) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Subsurface Opacity", &selectedPbrTextureSet->subsurfaceOpacity, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}

					ImGui::TreePop();
				}
				if (ImGui::TreeNodeEx("Coat")) {
					if (ImGui::ColorPicker3("Coat Color", &selectedPbrTextureSet->coatColor.red)) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Coat Strength", &selectedPbrTextureSet->coatStrength, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Coat Roughness", &selectedPbrTextureSet->coatRoughness, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Coat Specular Level", &selectedPbrTextureSet->coatSpecularLevel, 0.f, 1.f, "%.3f")) {
						wasEdited = true;
					}
					if (ImGui::SliderFloat("Inner Layer Displacement Offset", &selectedPbrTextureSet->innerLayerDisplacementOffset, 0.f, 3.f, "%.3f")) {
						wasEdited = true;
					}
					ImGui::TreePop();
				}
				if (ImGui::TreeNodeEx("Glint")) {
					if (ImGui::Checkbox("Enabled", &selectedPbrTextureSet->glintParameters.enabled)) {
						wasEdited = true;
					}
					if (selectedPbrTextureSet->glintParameters.enabled) {
						if (ImGui::SliderFloat("Screenspace Scale", &selectedPbrTextureSet->glintParameters.screenSpaceScale, 0.f, 3.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Log Microfacet Density", &selectedPbrTextureSet->glintParameters.logMicrofacetDensity, 0.f, 40.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Microfacet Roughness", &selectedPbrTextureSet->glintParameters.microfacetRoughness, 0.f, 1.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Density Randomization", &selectedPbrTextureSet->glintParameters.densityRandomization, 0.f, 5.f, "%.3f")) {
							wasEdited = true;
						}
					}
					ImGui::TreePop();
				}
				if (wasEdited) {
					for (auto& [material, extensions] : BSLightingShaderMaterialPBR::All) {
						if (extensions.textureSetData == selectedPbrTextureSet) {
							material->ApplyTextureSetData(*extensions.textureSetData);
						}
					}
					for (auto& [material, textureSets] : BSLightingShaderMaterialPBRLandscape::All) {
						for (uint32_t textureSetIndex = 0; textureSetIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureSetIndex) {
							if (textureSets[textureSetIndex] == selectedPbrTextureSet) {
								SetupPBRLandscapeTextureParameters(*material, *textureSets[textureSetIndex], textureSetIndex);
							}
						}
					}
				}
				if (selectedPbrTextureSet != nullptr) {
					if (ImGui::Button("Save")) {
						PNState::SavePBRRecordConfig("Data\\PBRTextureSets", selectedPbrTextureSetName, *selectedPbrTextureSet);
					}
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Material Object Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (Util::SearchableCombo("Material Object", selectedPbrMaterialObjectName, pbrMaterialObjects)) {
				selectedPbrMaterialObject = &pbrMaterialObjects[selectedPbrMaterialObjectName];
			}

			if (selectedPbrMaterialObject != nullptr) {
				bool wasEdited = false;
				if (ImGui::SliderFloat3("Base Color Scale", selectedPbrMaterialObject->baseColorScale.data(), 0.f, 10.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Roughness", &selectedPbrMaterialObject->roughness, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::SliderFloat("Specular Level", &selectedPbrMaterialObject->specularLevel, 0.f, 1.f, "%.3f")) {
					wasEdited = true;
				}
				if (ImGui::TreeNodeEx("Glint")) {
					if (ImGui::Checkbox("Enabled", &selectedPbrMaterialObject->glintParameters.enabled)) {
						wasEdited = true;
					}
					if (selectedPbrMaterialObject->glintParameters.enabled) {
						if (ImGui::SliderFloat("Screenspace Scale", &selectedPbrMaterialObject->glintParameters.screenSpaceScale, 0.f, 3.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Log Microfacet Density", &selectedPbrMaterialObject->glintParameters.logMicrofacetDensity, 0.f, 40.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Microfacet Roughness", &selectedPbrMaterialObject->glintParameters.microfacetRoughness, 0.f, 1.f, "%.3f")) {
							wasEdited = true;
						}
						if (ImGui::SliderFloat("Density Randomization", &selectedPbrMaterialObject->glintParameters.densityRandomization, 0.f, 5.f, "%.3f")) {
							wasEdited = true;
						}
					}
					ImGui::TreePop();
				}
				if (wasEdited) {
					for (auto& [material, extensions] : BSLightingShaderMaterialPBR::All) {
						if (extensions.materialObjectData == selectedPbrMaterialObject) {
							material->ApplyMaterialObjectData(*extensions.materialObjectData);
						}
					}
				}
				if (selectedPbrMaterialObject != nullptr) {
					if (ImGui::Button("Save")) {
						PNState::SavePBRRecordConfig("Data\\PBRMaterialObjects", selectedPbrMaterialObjectName, *selectedPbrMaterialObject);
					}
				}
			}
			ImGui::TreePop();
		}
	}
}

void TruePBR::SetupResources()
{
	SetupTextureSetData();
	SetupMaterialObjectData();
}

void TruePBR::PrePass()
{
}

void TruePBR::SetupGlintsTexture()
{
	
}

void TruePBR::SetupFrame()
{
	SetupDefaultPBRLandTextureSet();
}

void TruePBR::SetupTextureSetData()
{
	logger::info("[TruePBR] loading PBR texture set configs");

	pbrTextureSets.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		try {
			pbrTextureSets.insert_or_assign(editorId, config);
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});
}

void TruePBR::ReloadTextureSetData()
{
	logger::info("[TruePBR] reloading PBR texture set configs");

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		try {
			if (auto it = pbrTextureSets.find(editorId); it != pbrTextureSets.cend()) {
				it->second = config;
			}
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});

	for (const auto& [material, textureSets] : BSLightingShaderMaterialPBRLandscape::All) {
		for (uint32_t textureSetIndex = 0; textureSetIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureSetIndex) {
			SetupPBRLandscapeTextureParameters(*material, *textureSets[textureSetIndex], textureSetIndex);
		}
	}
}

TruePBR::PBRTextureSetData* TruePBR::GetPBRTextureSetData(const RE::TESForm* textureSet)
{
	if (textureSet == nullptr) {
		return nullptr;
	}

	auto it = pbrTextureSets.find(textureSet->GetFormEditorID());
	if (it == pbrTextureSets.end()) {
		return nullptr;
	}
	return &it->second;
}

bool TruePBR::IsPBRTextureSet(const RE::TESForm* textureSet)
{
	return GetPBRTextureSetData(textureSet) != nullptr;
}

void TruePBR::SetupMaterialObjectData()
{
	return;
	logger::info("[TruePBR] loading PBR material object configs");

	pbrMaterialObjects.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRMaterialObjects", [this](const std::string& editorId, const json& config) {
		try {
			pbrMaterialObjects.insert_or_assign(editorId, config);
		} catch (const std::exception& e) {
			logger::error("Failed to deserialize config for {}: {}.", editorId, e.what());
		}
	});
}

TruePBR::PBRMaterialObjectData* TruePBR::GetPBRMaterialObjectData(const RE::TESForm* materialObject)
{
	if (materialObject == nullptr) {
		return nullptr;
	}

	auto it = pbrMaterialObjects.find(materialObject->GetFormEditorID());
	if (it == pbrMaterialObjects.end()) {
		return nullptr;
	}
	return &it->second;
}

bool TruePBR::IsPBRMaterialObject(const RE::TESForm* materialObject)
{
	return GetPBRMaterialObjectData(materialObject) != nullptr;
}

namespace Permutations
{
	template <typename RangeType>
	std::unordered_set<uint32_t> GenerateFlagPermutations(const RangeType& flags, uint32_t constantFlags)
	{
		std::vector<uint32_t> flagValues;
		std::ranges::transform(flags, std::back_inserter(flagValues), [](auto flag) { return static_cast<uint32_t>(flag); });
		const uint32_t size = static_cast<uint32_t>(flagValues.size());

		std::unordered_set<uint32_t> result;
		for (uint32_t mask = 0; mask < (1u << size); ++mask) {
			uint32_t flag = constantFlags;
			for (size_t index = 0; index < size; ++index) {
				if (mask & (1 << index)) {
					flag |= flagValues[index];
				}
			}
			result.insert(flag);
		}

		return result;
	}

	uint32_t GetLightingShaderDescriptor(SIE::ShaderCache::LightingShaderTechniques technique, uint32_t flags)
	{
		return ((static_cast<uint32_t>(technique) & 0x3F) << 24) | flags;
	}

	void AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques technique, const std::unordered_set<uint32_t>& flags, std::unordered_set<uint32_t>& result)
	{
		for (uint32_t flag : flags) {
			result.insert(GetLightingShaderDescriptor(technique, flag));
		}
	}

	std::unordered_set<uint32_t> GeneratePBRLightingPixelPermutations()
	{
		using enum SIE::ShaderCache::LightingShaderFlags;

		constexpr std::array defaultFlags{ Deferred, AnisoLighting, Skinned, DoAlphaTest };
		constexpr std::array projectedUvFlags{ Deferred, AnisoLighting, DoAlphaTest, Snow };
		constexpr std::array lodObjectsFlags{ Deferred, WorldMap, DoAlphaTest, ProjectedUV };
		constexpr std::array treeFlags{ Deferred, AnisoLighting, Skinned, DoAlphaTest };
		constexpr std::array landFlags{ Deferred, AnisoLighting };

		constexpr uint32_t defaultConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC);
		constexpr uint32_t projectedUvConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC) | static_cast<uint32_t>(ProjectedUV);

		const std::unordered_set<uint32_t> defaultFlagValues = GenerateFlagPermutations(defaultFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> projectedUvFlagValues = GenerateFlagPermutations(projectedUvFlags, projectedUvConstantFlags);
		const std::unordered_set<uint32_t> lodObjectsFlagValues = GenerateFlagPermutations(lodObjectsFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> treeFlagValues = GenerateFlagPermutations(treeFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> landFlagValues = GenerateFlagPermutations(landFlags, defaultConstantFlags);

		std::unordered_set<uint32_t> result;
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, defaultFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, projectedUvFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjects, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::LODObjectHD, lodObjectsFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::TreeAnim, treeFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLand, landFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend, landFlagValues, result);
		return result;
	}
}

void TruePBR::GenerateShaderPermutations(RE::BSShader* shader)
{
	return;
	auto state = globals::state;
	auto shaderCache = globals::shaderCache;
	if (shader->shaderType == RE::BSShader::Type::Lighting) {
		const auto pixelPermutations = Permutations::GeneratePBRLightingPixelPermutations();
		for (auto descriptor : pixelPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			state->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache->GetPixelShader(*shader, pixelShaderDescriptor);
		}
	}
}

struct ExtendedRendererState
{
	static constexpr uint32_t NumPSTextures = 12;
	static constexpr uint32_t FirstPSTexture = 80;

	uint32_t PSResourceModifiedBits = 0;
	std::array<ID3D11ShaderResourceView*, NumPSTextures> PSTexture;

	void SetPSTexture(size_t textureIndex, RE::BSGraphics::Texture* newTexture)
	{
		ID3D11ShaderResourceView* resourceView = newTexture ? newTexture->resourceView : nullptr;
		//if (PSTexture[textureIndex] != resourceView)
		{
			PSTexture[textureIndex] = resourceView;
			PSResourceModifiedBits |= (1 << textureIndex);
		}
	}

	ExtendedRendererState()
	{
		std::fill(PSTexture.begin(), PSTexture.end(), nullptr);
	}
} extendedRendererState;

struct BSLightingShaderProperty_LoadBinary
{
	static void thunk(RE::BSLightingShaderProperty* property, RE::NiStream& stream)
	{
		using enum RE::BSShaderProperty::EShaderPropertyFlag;

		RE::BSShaderMaterial::Feature feature = RE::BSShaderMaterial::Feature::kDefault;
		stream.iStr->read(&feature, 1);

		{
			auto vtable = REL::Relocation<void***>(RE::NiShadeProperty::VTABLE[0]);
			auto baseMethod = reinterpret_cast<void (*)(RE::NiShadeProperty*, RE::NiStream&)>((vtable.get()[0x18]));
			baseMethod(property, stream);
		}

		stream.iStr->read(&property->flags, 1);

		RE::BSLightingShaderMaterialBase* material = RE::BSLightingShaderMaterialBase::CreateMaterial(feature);

		property->LinkMaterial(nullptr, false);
		property->material = material;

		{
			stream.iStr->read(&property->material->texCoordOffset[0].x, 1);
			stream.iStr->read(&property->material->texCoordOffset[0].y, 1);
			stream.iStr->read(&property->material->texCoordScale[0].x, 1);
			stream.iStr->read(&property->material->texCoordScale[0].y, 1);

			property->material->texCoordOffset[1] = property->material->texCoordOffset[0];
			property->material->texCoordScale[1] = property->material->texCoordScale[0];
		}

		stream.LoadLinkID();

		{
			RE::NiColor emissiveColor{};
			stream.iStr->read(&emissiveColor.red, 1);
			stream.iStr->read(&emissiveColor.green, 1);
			stream.iStr->read(&emissiveColor.blue, 1);

			if (property->emissiveColor != nullptr && property->flags.any(kOwnEmit)) {
				*property->emissiveColor = emissiveColor;
			}
		}

		stream.iStr->read(&property->emissiveMult, 1);

		static_cast<RE::BSLightingShaderMaterialBase*>(property->material)->LoadBinary(stream);
		
		material->ClearTextures();
		/*const auto& stateData = globals::game::graphicsState->GetRuntimeData();
		material->diffuseTexture = stateData.defaultTextureGrey;
		material->normalTexture = stateData.defaultTextureNormalMap;
		material->rimSoftLightingTexture = stateData.defaultTextureGrey;
		material->specularBackLightingTexture = stateData.defaultTextureGrey;*/
		

		//property->flags.reset(kVertexLighting, kMenuScreen, kSpecular, kEnvMap, kMultiLayerParallax, kSoftLighting, kRimLighting, kBackLighting, kAnisotropicLighting, kEffectLighting, kFitSlope);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShaderProperty_GetRenderPasses
{
	static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
	{
		auto renderPasses = func(property, geometry, renderFlags, accumulator);
		if (renderPasses == nullptr) {
			return renderPasses;
		}

		//const auto issEnabledAndInteriorWithSun = globals::features::interiorSun.loaded && globals::features::interiorSun.isInteriorWithSun;

		bool isPbr = false;

		if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting) && (property->material->GetFeature() == RE::BSShaderMaterial::Feature::kDefault || property->material->GetFeature() == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend)) {
			isPbr = true;
		}

	
		return renderPasses;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

bool TruePBR::BSLightingShader_SetupMaterial(RE::BSLightingShader*, RE::BSLightingShaderMaterialBase const*)
{
	return false;
}

struct BSLightingShader_SetupGeometry
{
	static void thunk(RE::BSLightingShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
	{
		const auto originalTechnique = shader->currentRawTechnique;

		if ((shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr)) != 0) {
			shader->currentRawTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AmbientSpecular);
			shader->currentRawTechnique ^= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AnisoLighting);
		}

		shader->currentRawTechnique &= ~0b111000u;
		shader->currentRawTechnique |= (std::min((pass->numLights - 1), 7) << 3);

		func(shader, pass, renderFlags);

		shader->currentRawTechnique = originalTechnique;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShader_GetPixelTechnique
{
	static uint32_t thunk(uint32_t rawTechnique)
	{
		uint32_t pixelTechnique = rawTechnique;

		pixelTechnique &= ~0b111000000u;
		if ((pixelTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ModelSpaceNormals)) == 0) {
			pixelTechnique &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Skinned);
		}
		pixelTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::VC);

		return pixelTechnique;
	}
};

void SetupPBRLandscapeTextureParameters(BSLightingShaderMaterialPBRLandscape& material, const TruePBR::PBRTextureSetData& textureSetData, uint32_t textureIndex)
{
	material.displacementScales[textureIndex] = textureSetData.displacementScale;
	material.roughnessScales[textureIndex] = textureSetData.roughnessScale;
	material.specularLevels[textureIndex] = textureSetData.specularLevel;
	material.glintParameters[textureIndex] = textureSetData.glintParameters;
}

void SetupLandscapeTexture(BSLightingShaderMaterialPBRLandscape& material, RE::TESLandTexture& landTexture, uint32_t textureIndex, std::array<TruePBR::PBRTextureSetData*, BSLightingShaderMaterialPBRLandscape::NumTiles>& textureSets)
{
	if (textureIndex >= 6) {
		return;
	}

	auto textureSet = Util::GetSeasonalSwap(landTexture.textureSet);
	if (textureSet == nullptr) {
		return;
	}

	auto truePBR = globals::truePBR;
	auto* textureSetData = truePBR->GetPBRTextureSetData(textureSet);
	const bool isPbr = textureSetData != nullptr;

	textureSets[textureIndex] = textureSetData;

	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::BaseColorTexture, material.landscapeBaseColorTextures[textureIndex]);
	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::NormalTexture, material.landscapeNormalTextures[textureIndex]);

	if (isPbr) {
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::RmaosTexture, material.landscapeRMAOSTextures[textureIndex]);
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::DisplacementTexture, material.landscapeDisplacementTextures[textureIndex]);
		SetupPBRLandscapeTextureParameters(material, *textureSetData, textureIndex);
	}
	material.isPbr[textureIndex] = isPbr;

	if (material.landscapeBaseColorTextures[textureIndex] != nullptr) {
		material.numLandscapeTextures = std::max(material.numLandscapeTextures, textureIndex + 1);
	}
}

RE::TESLandTexture* GetDefaultLandTexture()
{
	static const auto defaultLandTextureAddress = REL::Relocation<RE::TESLandTexture**>(RELOCATION_ID(514783, 400936));
	return *defaultLandTextureAddress;
}

bool TruePBR::TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
{
	if (land == nullptr) {
		return false;
	}

	return false;
}

struct TESForm_GetFormEditorID
{
	static const char* thunk(const RE::TESForm* form)
	{
		auto* singleton = globals::truePBR;
		auto it = singleton->editorIDs.find(form->GetFormID());
		if (it == singleton->editorIDs.cend()) {
			return "";
		}
		return it->second.c_str();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESForm_SetFormEditorID
{
	static bool thunk(RE::TESForm* form, const char* editorId)
	{
		auto* singleton = globals::truePBR;
		singleton->editorIDs[form->GetFormID()] = editorId;
		return true;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSTempEffectSimpleDecal_SetupGeometry
{
	static void thunk(RE::BSTempEffectSimpleDecal* decal, RE::BSGeometry* geometry, RE::BGSTextureSet* textureSet, bool blended)
	{
		func(decal, geometry, textureSet, blended);
		auto* singleton = globals::truePBR;
		auto unknownProperty = geometry->GetGeometryRuntimeData().properties[1].get();
		if (auto shaderProperty = unknownProperty->GetRTTI() == globals::rtti::BSLightingShaderPropertyRTTI.get() ? static_cast<RE::BSLightingShaderProperty*>(unknownProperty) : nullptr;
			shaderProperty != nullptr && singleton->IsPBRTextureSet(textureSet)) {
			{
				BSLightingShaderMaterialPBR srcMaterial;
				shaderProperty->LinkMaterial(&srcMaterial, true);
			}

			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
			pbrMaterial->OnLoadTextureSet(0, textureSet);

			constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
			*shaderProperty->emissiveColor = whiteColor;
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack;
			shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

			{
				using enum RE::BSShaderProperty::EShaderPropertyFlag8;
				shaderProperty->SetFlags(kParallaxOcclusion, false);
				shaderProperty->SetFlags(kParallax, false);
				shaderProperty->SetFlags(kGlowMap, false);
				shaderProperty->SetFlags(kEnvMap, false);
				shaderProperty->SetFlags(kSpecular, false);

				shaderProperty->SetFlags(kVertexLighting, true);
			}
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSTempEffectGeometryDecal_Initialize
{
	static void thunk(RE::BSTempEffectGeometryDecal* decal)
	{
		func(decal);
		auto* singleton = globals::truePBR;

		if (decal->decal != nullptr && singleton->IsPBRTextureSet(decal->texSet)) {
			auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(RE::MemoryManager::GetSingleton()->Allocate(sizeof(RE::BSLightingShaderProperty), 0, false));
			shaderProperty->Ctor();

			{
				BSLightingShaderMaterialPBR srcMaterial;
				shaderProperty->LinkMaterial(&srcMaterial, true);
			}

			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
			pbrMaterial->OnLoadTextureSet(0, decal->texSet);

			constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
			*shaderProperty->emissiveColor = whiteColor;
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != globals::game::graphicsState->GetRuntimeData().defaultTextureBlack;
			shaderProperty->emissiveMult = hasEmissive ? 1.f : 0.f;

			{
				using enum RE::BSShaderProperty::EShaderPropertyFlag8;

				shaderProperty->SetFlags(kSkinned, true);
				shaderProperty->SetFlags(kDynamicDecal, true);
				shaderProperty->SetFlags(kZBufferTest, true);
				shaderProperty->SetFlags(kZBufferWrite, false);

				shaderProperty->SetFlags(kVertexLighting, true);
			}

			if (auto* alphaProperty = static_cast<RE::NiAlphaProperty*>(decal->decal->GetGeometryRuntimeData().properties[0].get())) {
				alphaProperty->alphaFlags = (alphaProperty->alphaFlags & ~0x1FE) | 0xED;
			}

			shaderProperty->SetupGeometry(decal->decal.get());
			decal->decal->GetGeometryRuntimeData().properties[1] = RE::NiPointer(shaderProperty);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESBoundObject_Clone3D
{
	static RE::NiAVObject* thunk(RE::TESBoundObject* object, RE::TESObjectREFR* ref, bool arg3)
	{
		auto truePBR = globals::truePBR;
		auto* result = func(object, ref, arg3);
		if (result != nullptr && ref != nullptr && ref->data.objectReference != nullptr && ref->data.objectReference->formType == RE::FormType::Static) {
			auto* stat = static_cast<RE::TESObjectSTAT*>(ref->data.objectReference);
			if (stat->data.materialObj != nullptr && stat->data.materialObj->directionalData.singlePass) {
				if (auto* pbrData = truePBR->GetPBRMaterialObjectData(stat->data.materialObj)) {
					RE::BSVisit::TraverseScenegraphGeometries(result, [pbrData](RE::BSGeometry* geometry) {
						if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(geometry->GetGeometryRuntimeData().properties[1].get())) {
							if (shaderProperty->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting &&
								shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
								if (auto* material = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material)) {
									material->ApplyMaterialObjectData(*pbrData);
									BSLightingShaderMaterialPBR::All[material].materialObjectData = pbrData;
								}
							}
						}

						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
			}
		}
		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BGSTextureSet_ToShaderTextureSet
{
	static RE::BSShaderTextureSet* thunk(RE::BGSTextureSet* textureSet)
	{
		auto truePBR = globals::truePBR;
		truePBR->currentTextureSet = textureSet;

		return func(textureSet);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShaderProperty_OnLoadTextureSet
{
	static void thunk(RE::BSLightingShaderProperty* property, void* a2)
	{
		func(property, a2);

		auto truePBR = globals::truePBR;
		truePBR->currentTextureSet = nullptr;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void TruePBR::PostPostLoad()
{
	logger::info("Hooking BGSTextureSet");
	stl::detour_thunk<BGSTextureSet_ToShaderTextureSet>(REL::RelocationID(20905, 21361));

	logger::info("Hooking BSLightingShaderProperty");
	stl::write_vfunc<0x18, BSLightingShaderProperty_LoadBinary>(RE::VTABLE_BSLightingShaderProperty[0]);
	//stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);
	stl::detour_thunk<BSLightingShaderProperty_OnLoadTextureSet>(REL::RelocationID(99865, 106510));

	logger::info("Hooking BSLightingShader");
	//stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
	//stl::detour_thunk_ignore_func<BSLightingShader_GetPixelTechnique>(REL::RelocationID(101633, 108700));

	logger::info("Hooking TESLandTexture");
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_TESLandTexture[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSTextureSet[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSMaterialObject[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSMaterialObject[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_BGSLightingTemplate[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_BGSLightingTemplate[0]);
	stl::write_vfunc<0x32, TESForm_GetFormEditorID>(RE::VTABLE_TESWeather[0]);
	stl::write_vfunc<0x33, TESForm_SetFormEditorID>(RE::VTABLE_TESWeather[0]);

	logger::info("Hooking BSTempEffectSimpleDecal");
	//stl::detour_thunk<BSTempEffectSimpleDecal_SetupGeometry>(REL::RelocationID(29253, 30108));

	logger::info("Hooking BSTempEffectGeometryDecal");
	//stl::write_vfunc<0x25, BSTempEffectGeometryDecal_Initialize>(RE::VTABLE_BSTempEffectGeometryDecal[0]);

	logger::info("Hooking TESObjectSTAT");
	//stl::write_vfunc<0x4A, TESBoundObject_Clone3D>(RE::VTABLE_TESObjectSTAT[0]);
}

void TruePBR::DataLoaded()
{
	defaultPbrLandTextureSet = RE::TESForm::LookupByEditorID<RE::BGSTextureSet>("DefaultPBRLand");
	SetupDefaultPBRLandTextureSet();
}

void TruePBR::SetupDefaultPBRLandTextureSet()
{
	if (!defaultLandTextureSetReplaced && defaultPbrLandTextureSet != nullptr) {
		if (auto* defaultLandTexture = GetDefaultLandTexture()) {
			logger::info("[TruePBR] replacing default land texture set record with {}", defaultPbrLandTextureSet->GetFormEditorID());
			defaultLandTexture->textureSet = defaultPbrLandTextureSet;
			defaultLandTextureSetReplaced = true;
		}
	}
}

void TruePBR::SetShaderResouces(ID3D11DeviceContext*)
{

}