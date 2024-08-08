#include "TruePBR.h"

#include <detours/Detours.h>

#include "TruePBR/BSLightingShaderMaterialPBR.h"
#include "TruePBR/BSLightingShaderMaterialPBRLandscape.h"

#include "Hooks.h"
#include "ShaderCache.h"
#include "State.h"

namespace PNState
{
	template <typename ResultType>
	bool Read(const json& config, const std::string_view& key, ResultType& result)
	{
		if (!config.is_object()) {
			return false;
		}

		auto it = config.find(key);
		if (it == config.end()) {
			return false;
		}

		const json& section = it.value();

		if constexpr (std::is_same_v<ResultType, std::array<float, 3>> || std::is_same_v<ResultType, RE::NiColor>) {
			if (section.is_array() && section.size() == 3 &&
				section[0].is_number_float() && section[1].is_number_float() &&
				section[2].is_number_float()) {
				result[0] = section[0];
				result[1] = section[1];
				result[2] = section[2];
				return true;
			}
		}
		if constexpr (std::is_same_v<ResultType, float>) {
			if (section.is_number_float()) {
				result = section;
				return true;
			}
		}
		return false;
	}

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

void TruePBR::DrawSettings()
{
	if (ImGui::TreeNodeEx("PBR", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (const auto* currentCell = player->GetParentCell()) {
				if (currentCell->IsInteriorCell()) {
					if (const auto* lightingTemplate = currentCell->GetRuntimeData().lightingTemplate) {
						if (ImGui::TreeNodeEx("Lighting Template Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
							const auto* editorId = lightingTemplate->GetFormEditorID();
							ImGui::Text(std::format("Current Lighting Template : {}", editorId).c_str());

							auto& pbrData = pbrLightingTemplates[editorId];

							ImGui::SliderFloat("Directional Light Scale", &pbrData.directionalLightColorScale, 0.f, 5.f);
							ImGui::SliderFloat("Directional Ambient Light Scale", &pbrData.directionalAmbientLightColorScale, 0.f, 5.f);

							if (ImGui::Button("Save")) {
								SavePBRLightingTemplateData(editorId);
							}

							ImGui::TreePop();
						}
					}
				} else if (RE::Sky* sky = RE::Sky::GetSingleton()) {
					if (const auto* weather = sky->currentWeather) {
						if (ImGui::TreeNodeEx("Weather Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
							const auto* editorId = weather->GetFormEditorID();
							ImGui::Text(std::format("Current Weather : {}", editorId).c_str());

							auto& pbrData = pbrWeathers[editorId];

							ImGui::SliderFloat("Directional Light Scale", &pbrData.directionalLightColorScale, 0.f, 5.f);
							ImGui::SliderFloat("Directional Ambient Light Scale", &pbrData.directionalAmbientLightColorScale, 0.f, 5.f);

							if (ImGui::Button("Save")) {
								SavePBRWeatherData(editorId);
							}

							ImGui::TreePop();
						}
					}
				}
			}
		}

		bool useMultipleScattering = settings.useMultipleScattering;
		bool useMultiBounceAO = settings.useMultiBounceAO;
		if (ImGui::Checkbox("Use Multiple Scattering", &useMultipleScattering)) {
			settings.useMultipleScattering = useMultipleScattering;
		}
		if (ImGui::Checkbox("Use Multi-bounce AO", &useMultiBounceAO)) {
			settings.useMultiBounceAO = useMultiBounceAO;
		}

		ImGui::SliderFloat("Direct Light Color Multiplier", &globalPBRDirectLightColorMultiplier, 1e-3f, 1e2f, "%.3f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Ambient Light Color Multiplier", &globalPBRAmbientLightColorMultiplier, 1e-3f, 1e2f, "%.3f", ImGuiSliderFlags_Logarithmic);
		ImGui::TreePop();
	}
}

void TruePBR::SetupResources()
{
	SetupTextureSetData();
	SetupMaterialObjectData();
	SetupLightingTemplateData();
	SetupWeatherData();
}

void TruePBR::LoadSettings(json& o_json)
{
	if (o_json["Use Multiple Scattering"].is_boolean()) {
		settings.useMultipleScattering = o_json["Use Multiple Scattering"];
	}
	if (o_json["Use Multi-bounce AO"].is_boolean()) {
		settings.useMultiBounceAO = o_json["Use Multi-bounce AO"];
	}

	if (o_json["Direct Light Color Multiplier"].is_number_float()) {
		globalPBRDirectLightColorMultiplier = o_json["Direct Light Color Multiplier"];
	}
	if (o_json["Ambient Light Color Multiplier"].is_number_float()) {
		globalPBRAmbientLightColorMultiplier = o_json["Ambient Light Color Multiplier"];
	}
}

void TruePBR::SaveSettings(json& o_json)
{
	o_json["Use Multiple Scattering"] = (bool)settings.useMultipleScattering;
	o_json["Use Multi-bounce AO"] = (bool)settings.useMultiBounceAO;

	o_json["Direct Light Color Multiplier"] = globalPBRDirectLightColorMultiplier;
	o_json["Ambient Light Color Multiplier"] = globalPBRAmbientLightColorMultiplier;
}

void TruePBR::SetupFrame()
{
	float newDirectionalLightScale = 1.f;
	float newDirectionalAmbientLightScale = 1.f;

	if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
		if (const auto* currentCell = player->GetParentCell()) {
			if (currentCell->IsInteriorCell()) {
				if (const auto* lightingTemplate = currentCell->GetRuntimeData().lightingTemplate) {
					const auto* editorId = lightingTemplate->GetFormEditorID();
					if (auto it = pbrLightingTemplates.find(editorId); it != pbrLightingTemplates.cend()) {
						newDirectionalLightScale = it->second.directionalLightColorScale;
						newDirectionalAmbientLightScale = it->second.directionalAmbientLightColorScale;
					}
				}
			} else if (RE::Sky* sky = RE::Sky::GetSingleton()) {
				if (const auto* weather = sky->currentWeather) {
					const auto* editorId = weather->GetFormEditorID();
					if (auto it = pbrWeathers.find(editorId); it != pbrWeathers.cend()) {
						newDirectionalLightScale = it->second.directionalLightColorScale;
						newDirectionalAmbientLightScale = it->second.directionalAmbientLightColorScale;
					}
				}
			}
		}
	}

	weatherPBRDirectionalLightColorMultiplier = newDirectionalLightScale;
	weatherPBRDirectionalAmbientLightColorMultiplier = newDirectionalAmbientLightScale;

	settings.directionalLightColorMultiplier = globalPBRDirectLightColorMultiplier * weatherPBRDirectionalLightColorMultiplier;
	settings.pointLightColorMultiplier = globalPBRDirectLightColorMultiplier;
	settings.ambientLightColorMultiplier = globalPBRAmbientLightColorMultiplier * weatherPBRDirectionalAmbientLightColorMultiplier;
}

void TruePBR::SetupTextureSetData()
{
	logger::info("[TruePBR] loading PBR texture set configs");

	pbrTextureSets.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRTextureSets", [this](const std::string& editorId, const json& config) {
		PBRTextureSetData textureSetData;

		PNState::Read(config, "roughnessScale", textureSetData.roughnessScale);
		PNState::Read(config, "displacementScale", textureSetData.displacementScale);
		PNState::Read(config, "specularLevel", textureSetData.specularLevel);
		PNState::Read(config, "subsurfaceColor", textureSetData.subsurfaceColor);
		PNState::Read(config, "subsurfaceOpacity", textureSetData.subsurfaceOpacity);
		PNState::Read(config, "coatColor", textureSetData.coatColor);
		PNState::Read(config, "coatStrength", textureSetData.coatStrength);
		PNState::Read(config, "coatRoughness", textureSetData.coatRoughness);
		PNState::Read(config, "coatSpecularLevel", textureSetData.coatSpecularLevel);
		PNState::Read(config, "innerLayerDisplacementOffset", textureSetData.innerLayerDisplacementOffset);
		PNState::Read(config, "fuzzColor", textureSetData.fuzzColor);
		PNState::Read(config, "fuzzWeight", textureSetData.fuzzWeight);

		pbrTextureSets.insert_or_assign(editorId, textureSetData);
	});
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
	logger::info("[TruePBR] loading PBR material object configs");

	pbrMaterialObjects.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRMaterialObjects", [this](const std::string& editorId, const json& config) {
		PBRMaterialObjectData materialObjectData;

		PNState::Read(config, "baseColorScale", materialObjectData.baseColorScale);
		PNState::Read(config, "roughness", materialObjectData.roughness);
		PNState::Read(config, "specularLevel", materialObjectData.specularLevel);

		pbrMaterialObjects.insert_or_assign(editorId, materialObjectData);
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

void TruePBR::SetupLightingTemplateData()
{
	logger::info("[TruePBR] loading PBR lighting template configs");

	pbrLightingTemplates.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRLightingTemplates", [this](const std::string& editorId, const json& config) {
		PBRLightingTemplateData lightingTemplateData;

		PNState::Read(config, "directionalLightColorScale", lightingTemplateData.directionalLightColorScale);
		PNState::Read(config, "directionalAmbientLightColorScale", lightingTemplateData.directionalAmbientLightColorScale);

		pbrLightingTemplates.insert_or_assign(editorId, lightingTemplateData);
	});
}

TruePBR::PBRLightingTemplateData* TruePBR::GetPBRLightingTemplateData(const RE::TESForm* lightingTemplate)
{
	if (lightingTemplate == nullptr) {
		return nullptr;
	}

	auto it = pbrLightingTemplates.find(lightingTemplate->GetFormEditorID());
	if (it == pbrLightingTemplates.end()) {
		return nullptr;
	}
	return &it->second;
}

bool TruePBR::IsPBRLightingTemplate(const RE::TESForm* lightingTemplate)
{
	return GetPBRLightingTemplateData(lightingTemplate) != nullptr;
}

void TruePBR::SavePBRLightingTemplateData(const std::string& editorId)
{
	const auto& pbrLightingTemplateData = pbrLightingTemplates[editorId];

	json config;
	config["directionalLightColorScale"] = pbrLightingTemplateData.directionalLightColorScale;
	config["directionalAmbientLightColorScale"] = pbrLightingTemplateData.directionalAmbientLightColorScale;

	PNState::SavePBRRecordConfig("Data\\PBRLightingTemplates\\", editorId, config);
}

void TruePBR::SetupWeatherData()
{
	logger::info("[TruePBR] loading PBR weather configs");

	pbrWeathers.clear();

	PNState::ReadPBRRecordConfigs("Data\\PBRWeathers", [this](const std::string& editorId, const json& config) {
		PBRWeatherData weatherData;

		PNState::Read(config, "directionalLightColorScale", weatherData.directionalLightColorScale);
		PNState::Read(config, "directionalAmbientLightColorScale", weatherData.directionalAmbientLightColorScale);

		pbrWeathers.insert_or_assign(editorId, weatherData);
	});
}

TruePBR::PBRWeatherData* TruePBR::GetPBRWeatherData(const RE::TESForm* weather)
{
	if (weather == nullptr) {
		return nullptr;
	}

	auto it = pbrWeathers.find(weather->GetFormEditorID());
	if (it == pbrWeathers.end()) {
		return nullptr;
	}
	return &it->second;
}

bool TruePBR::IsPBRWeather(const RE::TESForm* weather)
{
	return GetPBRWeatherData(weather) != nullptr;
}

void TruePBR::SavePBRWeatherData(const std::string& editorId)
{
	const auto& pbrWeatherData = pbrWeathers[editorId];

	json config;
	config["directionalLightColorScale"] = pbrWeatherData.directionalLightColorScale;
	config["directionalAmbientLightColorScale"] = pbrWeatherData.directionalAmbientLightColorScale;

	PNState::SavePBRRecordConfig("Data\\PBRWeathers\\", editorId, config);
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

	std::unordered_set<uint32_t> GeneratePBRLightingVertexPermutations()
	{
		using enum SIE::ShaderCache::LightingShaderFlags;

		constexpr std::array defaultFlags{ VC, Skinned, WorldMap };
		constexpr std::array projectedUvFlags{ VC, WorldMap };
		constexpr std::array treeFlags{ VC, Skinned };
		constexpr std::array landFlags{ VC };

		constexpr uint32_t defaultConstantFlags = static_cast<uint32_t>(TruePbr);
		constexpr uint32_t projectedUvConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(ProjectedUV);

		const std::unordered_set<uint32_t> defaultFlagValues = GenerateFlagPermutations(defaultFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> projectedUvFlagValues = GenerateFlagPermutations(projectedUvFlags, projectedUvConstantFlags);
		const std::unordered_set<uint32_t> treeFlagValues = GenerateFlagPermutations(treeFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> landFlagValues = GenerateFlagPermutations(landFlags, defaultConstantFlags);

		std::unordered_set<uint32_t> result;
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, defaultFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::None, projectedUvFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::TreeAnim, treeFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLand, landFlagValues, result);
		AddLightingShaderDescriptors(SIE::ShaderCache::LightingShaderTechniques::MTLandLODBlend, landFlagValues, result);
		return result;
	}

	std::unordered_set<uint32_t> GeneratePBRLightingPixelPermutations()
	{
		using enum SIE::ShaderCache::LightingShaderFlags;

		constexpr std::array defaultFlags{ Skinned, DoAlphaTest, AdditionalAlphaMask };
		constexpr std::array projectedUvFlags{ DoAlphaTest, AdditionalAlphaMask, Snow, BaseObjectIsSnow };
		constexpr std::array lodObjectsFlags{ WorldMap, DoAlphaTest, AdditionalAlphaMask, ProjectedUV };
		constexpr std::array treeFlags{ Skinned, DoAlphaTest, AdditionalAlphaMask };

		constexpr uint32_t defaultConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC);
		constexpr uint32_t projectedUvConstantFlags = static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(VC) | static_cast<uint32_t>(ProjectedUV);

		const std::unordered_set<uint32_t> defaultFlagValues = GenerateFlagPermutations(defaultFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> projectedUvFlagValues = GenerateFlagPermutations(projectedUvFlags, projectedUvConstantFlags);
		const std::unordered_set<uint32_t> lodObjectsFlagValues = GenerateFlagPermutations(lodObjectsFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> treeFlagValues = GenerateFlagPermutations(treeFlags, defaultConstantFlags);
		const std::unordered_set<uint32_t> landFlagValues = { defaultConstantFlags };

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

	std::unordered_set<uint32_t> GeneratePBRGrassPermutations()
	{
		using enum SIE::ShaderCache::GrassShaderTechniques;
		using enum SIE::ShaderCache::GrassShaderFlags;

		return { static_cast<uint32_t>(TruePbr),
			static_cast<uint32_t>(TruePbr) | static_cast<uint32_t>(AlphaTest) };
	}

	std::unordered_set<uint32_t> GeneratePBRGrassVertexPermutations()
	{
		return GeneratePBRGrassPermutations();
	}

	std::unordered_set<uint32_t> GeneratePBRGrassPixelPermutations()
	{
		return GeneratePBRGrassPermutations();
	}
}

void TruePBR::GenerateShaderPermutations(RE::BSShader* shader)
{
	auto& shaderCache = SIE::ShaderCache::Instance();
	if (shader->shaderType == RE::BSShader::Type::Lighting) {
		const auto vertexPermutations = Permutations::GeneratePBRLightingVertexPermutations();
		for (auto descriptor : vertexPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache.GetVertexShader(*shader, vertexShaderDesriptor);
		}

		const auto pixelPermutations = Permutations::GeneratePBRLightingPixelPermutations();
		for (auto descriptor : pixelPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache.GetPixelShader(*shader, pixelShaderDescriptor);
		}
	} else if (shader->shaderType == RE::BSShader::Type::Grass) {
		const auto vertexPermutations = Permutations::GeneratePBRGrassVertexPermutations();
		for (auto descriptor : vertexPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache.GetVertexShader(*shader, vertexShaderDesriptor);
		}

		const auto pixelPermutations = Permutations::GeneratePBRGrassPixelPermutations();
		for (auto descriptor : pixelPermutations) {
			auto vertexShaderDesriptor = descriptor;
			auto pixelShaderDescriptor = descriptor;
			State::GetSingleton()->ModifyShaderLookup(*shader, vertexShaderDesriptor, pixelShaderDescriptor);
			std::ignore = shaderCache.GetPixelShader(*shader, pixelShaderDescriptor);
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

		bool isPbr = false;
		{
			RE::BSLightingShaderMaterialBase* material = nullptr;
			if (property->flags.any(kMenuScreen)) {
				auto* pbrMaterial = BSLightingShaderMaterialPBR::Make();
				pbrMaterial->loadedWithFeature = feature;
				material = pbrMaterial;
				isPbr = true;
			} else {
				material = RE::BSLightingShaderMaterialBase::CreateMaterial(feature);
			}
			property->LinkMaterial(nullptr, false);
			property->material = material;
		}

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

		if (isPbr) {
			auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(property->material);
			if (property->flags.any(kMultiLayerParallax)) {
				pbrMaterial->pbrFlags.set(PBRFlags::TwoLayer);
				if (property->flags.any(kSoftLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::InterlayerParallax);
				}
				if (property->flags.any(kBackLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::CoatNormal);
				}
				if (property->flags.any(kEffectLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::ColoredCoat);
				}
			} else if (property->flags.any(kBackLighting)) {
				pbrMaterial->pbrFlags.set(PBRFlags::HairMarschner);
			} else {
				if (property->flags.any(kRimLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::Subsurface);
				}
				if (property->flags.any(kSoftLighting)) {
					pbrMaterial->pbrFlags.set(PBRFlags::Fuzz);
				}
			}
			property->flags.set(kVertexLighting);
			property->flags.reset(kMenuScreen, kSpecular, kGlowMap, kEnvMap, kMultiLayerParallax, kSoftLighting, kRimLighting, kBackLighting, kAnisotropicLighting, kEffectLighting);
		}
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

		bool isPbr = false;

		if (property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting) && (property->material->GetFeature() == RE::BSShaderMaterial::Feature::kDefault || property->material->GetFeature() == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend)) {
			isPbr = true;
		}

		auto currentPass = renderPasses->head;
		while (currentPass != nullptr) {
			if (currentPass->shader->shaderType == RE::BSShader::Type::Lighting) {
				constexpr uint32_t LightingTechniqueStart = 0x4800002D;
				auto lightingTechnique = currentPass->passEnum - LightingTechniqueStart;
				auto lightingFlags = lightingTechnique & ~(~0u << 24);
				auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((lightingTechnique >> 24) & 0x3F);
				lightingFlags &= ~0b111000u;
				if (isPbr) {
					lightingFlags |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr);
				}
				lightingTechnique = (static_cast<uint32_t>(lightingType) << 24) | lightingFlags;
				currentPass->passEnum = lightingTechnique + LightingTechniqueStart;
			}
			currentPass = currentPass->next;
		}

		return renderPasses;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShader_SetupMaterial
{
	static void thunk(RE::BSLightingShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		using enum SIE::ShaderCache::LightingShaderTechniques;

		auto lightingFlags = shader->currentRawTechnique & ~(~0u << 24);
		auto lightingType = static_cast<SIE::ShaderCache::LightingShaderTechniques>((shader->currentRawTechnique >> 24) & 0x3F);
		if (!(lightingType == LODLand || lightingType == LODLandNoise) && (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr))) {
			auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			auto renderer = RE::BSGraphics::Renderer::GetSingleton();

			RE::BSGraphics::Renderer::PrepareVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::PreparePSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);

			if (lightingType == MTLand || lightingType == MTLandLODBlend) {
				auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBRLandscape*>(material);

				constexpr size_t NormalStartIndex = 7;

				for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
					if (pbrMaterial->landscapeBaseColorTextures[textureIndex] != nullptr) {
						shadowState->SetPSTexture(textureIndex, pbrMaterial->landscapeBaseColorTextures[textureIndex]->rendererTexture);
						shadowState->SetPSTextureAddressMode(textureIndex, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
						shadowState->SetPSTextureFilterMode(textureIndex, RE::BSGraphics::TextureFilterMode::kAnisotropic);
					}
					if (pbrMaterial->landscapeNormalTextures[textureIndex] != nullptr) {
						const uint32_t normalTextureIndex = NormalStartIndex + textureIndex;
						shadowState->SetPSTexture(normalTextureIndex, pbrMaterial->landscapeNormalTextures[textureIndex]->rendererTexture);
						shadowState->SetPSTextureAddressMode(normalTextureIndex, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
						shadowState->SetPSTextureFilterMode(normalTextureIndex, RE::BSGraphics::TextureFilterMode::kAnisotropic);
					}
					if (pbrMaterial->landscapeDisplacementTextures[textureIndex] != nullptr) {
						extendedRendererState.SetPSTexture(textureIndex, pbrMaterial->landscapeDisplacementTextures[textureIndex]->rendererTexture);
					}
					if (pbrMaterial->landscapeRMAOSTextures[textureIndex] != nullptr) {
						extendedRendererState.SetPSTexture(BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex, pbrMaterial->landscapeRMAOSTextures[textureIndex]->rendererTexture);
					}
				}

				if (pbrMaterial->terrainOverlayTexture != nullptr) {
					shadowState->SetPSTexture(13, pbrMaterial->terrainOverlayTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(13, RE::BSGraphics::TextureAddressMode::kClampSClampT);
					shadowState->SetPSTextureFilterMode(13, RE::BSGraphics::TextureFilterMode::kAnisotropic);
				}

				if (pbrMaterial->terrainNoiseTexture != nullptr) {
					shadowState->SetPSTexture(15, pbrMaterial->terrainNoiseTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(15, RE::BSGraphics::TextureAddressMode::kWrapSWrapT);
					shadowState->SetPSTextureFilterMode(15, RE::BSGraphics::TextureFilterMode::kBilinear);
				}

				{
					uint32_t flags = 0;
					for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
						if (pbrMaterial->isPbr[textureIndex]) {
							flags |= (1 << textureIndex);
							if (pbrMaterial->landscapeDisplacementTextures[textureIndex] != nullptr && pbrMaterial->landscapeDisplacementTextures[textureIndex] != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack) {
								flags |= (1 << (BSLightingShaderMaterialPBRLandscape::NumTiles + textureIndex));
							}
						}
					}
					shadowState->SetPSConstant(flags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 36);
				}

				{
					constexpr size_t PBRParamsStartIndex = 37;

					for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
						std::array<float, 3> PBRParams;
						PBRParams[0] = pbrMaterial->roughnessScales[textureIndex];
						PBRParams[1] = pbrMaterial->displacementScales[textureIndex];
						PBRParams[2] = pbrMaterial->specularLevels[textureIndex];
						shadowState->SetPSConstant(PBRParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, PBRParamsStartIndex + textureIndex);
					}
				}

				{
					std::array<float, 4> lodTexParams;
					lodTexParams[0] = pbrMaterial->terrainTexOffsetX;
					lodTexParams[1] = pbrMaterial->terrainTexOffsetY;
					lodTexParams[2] = 1.f;
					lodTexParams[3] = pbrMaterial->terrainTexFade;
					shadowState->SetPSConstant(lodTexParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 24);
				}
			} else if (lightingType == None || lightingType == TreeAnim) {
				auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBR*>(material);
				if (pbrMaterial->diffuseRenderTargetSourceIndex != -1) {
					shadowState->SetPSTexture(0, renderer->GetRuntimeData().renderTargets[pbrMaterial->diffuseRenderTargetSourceIndex]);
				} else {
					shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
				}
				shadowState->SetPSTextureAddressMode(0, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shadowState->SetPSTexture(1, pbrMaterial->normalTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(1, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(1, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shadowState->SetPSTexture(5, pbrMaterial->rmaosTexture->rendererTexture);
				shadowState->SetPSTextureAddressMode(5, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(5, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				stl::enumeration<PBRShaderFlags> shaderFlags;
				if (pbrMaterial->pbrFlags.any(PBRFlags::TwoLayer)) {
					shaderFlags.set(PBRShaderFlags::TwoLayer);
					if (pbrMaterial->pbrFlags.any(PBRFlags::InterlayerParallax)) {
						shaderFlags.set(PBRShaderFlags::InterlayerParallax);
					}
					if (pbrMaterial->pbrFlags.any(PBRFlags::CoatNormal)) {
						shaderFlags.set(PBRShaderFlags::CoatNormal);
					}
					if (pbrMaterial->pbrFlags.any(PBRFlags::ColoredCoat)) {
						shaderFlags.set(PBRShaderFlags::ColoredCoat);
					}

					std::array<float, 4> PBRParams2;
					PBRParams2[0] = pbrMaterial->GetCoatColor().red;
					PBRParams2[1] = pbrMaterial->GetCoatColor().green;
					PBRParams2[2] = pbrMaterial->GetCoatColor().blue;
					PBRParams2[3] = pbrMaterial->GetCoatStrength();
					shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 43);

					std::array<float, 4> PBRParams3;
					PBRParams3[0] = pbrMaterial->GetCoatRoughness();
					PBRParams3[1] = pbrMaterial->GetCoatSpecularLevel();
					shadowState->SetPSConstant(PBRParams3, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 27);
				} else if (pbrMaterial->pbrFlags.any(PBRFlags::HairMarschner)) {
					shaderFlags.set(PBRShaderFlags::HairMarschner);
				} else {
					if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
						shaderFlags.set(PBRShaderFlags::Subsurface);

						std::array<float, 4> PBRParams2;
						PBRParams2[0] = pbrMaterial->GetSubsurfaceColor().red;
						PBRParams2[1] = pbrMaterial->GetSubsurfaceColor().green;
						PBRParams2[2] = pbrMaterial->GetSubsurfaceColor().blue;
						PBRParams2[3] = pbrMaterial->GetSubsurfaceOpacity();
						shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 43);
					}
					if (pbrMaterial->pbrFlags.any(PBRFlags::Fuzz)) {
						shaderFlags.set(PBRShaderFlags::Fuzz);

						std::array<float, 4> PBRParams3;
						PBRParams3[0] = pbrMaterial->GetFuzzColor().red;
						PBRParams3[1] = pbrMaterial->GetFuzzColor().green;
						PBRParams3[2] = pbrMaterial->GetFuzzColor().blue;
						PBRParams3[3] = pbrMaterial->GetFuzzWeight();
						shadowState->SetPSConstant(PBRParams3, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 27);
					}
				}

				{
					std::array<float, 4> PBRProjectedUVParams1;
					PBRProjectedUVParams1[0] = pbrMaterial->GetProjectedMaterialBaseColorScale()[0];
					PBRProjectedUVParams1[1] = pbrMaterial->GetProjectedMaterialBaseColorScale()[1];
					PBRProjectedUVParams1[2] = pbrMaterial->GetProjectedMaterialBaseColorScale()[2];
					shadowState->SetPSConstant(PBRProjectedUVParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 21);

					std::array<float, 4> PBRProjectedUVParams2;
					PBRProjectedUVParams2[0] = pbrMaterial->GetProjectedMaterialRoughness();
					PBRProjectedUVParams2[1] = pbrMaterial->GetProjectedMaterialSpecularLevel();
					shadowState->SetPSConstant(PBRProjectedUVParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 22);
				}

				const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
				if (hasEmissive) {
					shadowState->SetPSTexture(6, pbrMaterial->emissiveTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(6, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(6, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasEmissive);
				}

				const bool hasDisplacement = pbrMaterial->displacementTexture != nullptr && pbrMaterial->displacementTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
				if (hasDisplacement) {
					shadowState->SetPSTexture(4, pbrMaterial->displacementTexture->rendererTexture);
					shadowState->SetPSTextureAddressMode(4, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(4, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasDisplacement);
				}

				const bool hasFeaturesTexture0 = pbrMaterial->featuresTexture0 != nullptr && pbrMaterial->featuresTexture0 != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureWhite;
				if (hasFeaturesTexture0) {
					shadowState->SetPSTexture(12, pbrMaterial->featuresTexture0->rendererTexture);
					shadowState->SetPSTextureAddressMode(12, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(12, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasFeaturesTexture0);
				}

				const bool hasFeaturesTexture1 = pbrMaterial->featuresTexture1 != nullptr && pbrMaterial->featuresTexture1 != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureWhite;
				if (hasFeaturesTexture1) {
					shadowState->SetPSTexture(9, pbrMaterial->featuresTexture1->rendererTexture);
					shadowState->SetPSTextureAddressMode(9, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
					shadowState->SetPSTextureFilterMode(9, RE::BSGraphics::TextureFilterMode::kAnisotropic);

					shaderFlags.set(PBRShaderFlags::HasFeaturesTexture1);
				}

				{
					shadowState->SetPSConstant(shaderFlags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 36);
				}

				{
					std::array<float, 3> PBRParams1;
					PBRParams1[0] = pbrMaterial->GetRoughnessScale();
					PBRParams1[1] = pbrMaterial->GetDisplacementScale();
					PBRParams1[2] = pbrMaterial->GetSpecularLevel();
					shadowState->SetPSConstant(PBRParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 37);
				}
			}

			{
				const uint32_t bufferIndex = RE::BSShaderManager::State::GetSingleton().textureTransformCurrentBuffer;

				std::array<float, 4> texCoordOffsetScale;
				texCoordOffsetScale[0] = material->texCoordOffset[bufferIndex].x;
				texCoordOffsetScale[1] = material->texCoordOffset[bufferIndex].y;
				texCoordOffsetScale[2] = material->texCoordScale[bufferIndex].x;
				texCoordOffsetScale[3] = material->texCoordScale[bufferIndex].y;
				shadowState->SetVSConstant(texCoordOffsetScale, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 11);
			}

			if (lightingFlags & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::CharacterLight)) {
				static const REL::Relocation<RE::ImageSpaceTexture*> characterLightTexture{ RELOCATION_ID(513464, 391302) };

				if (characterLightTexture->renderTarget >= RE::RENDER_TARGET::kFRAMEBUFFER) {
					shadowState->SetPSTexture(11, renderer->GetRuntimeData().renderTargets[characterLightTexture->renderTarget]);
					shadowState->SetPSTextureAddressMode(11, RE::BSGraphics::TextureAddressMode::kClampSClampT);
				}

				const auto& smState = RE::BSShaderManager::State::GetSingleton();
				std::array<float, 4> characterLightParams;
				if (smState.characterLightEnabled) {
					std::copy_n(smState.characterLightParams, 4, characterLightParams.data());
				} else {
					std::fill_n(characterLightParams.data(), 4, 0.f);
				}
				shadowState->SetPSConstant(characterLightParams, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 35);
			}

			RE::BSGraphics::Renderer::FlushVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::FlushPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::ApplyVSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::ApplyPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		} else {
			func(shader, material);
		}
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSLightingShader_SetupGeometry
{
	static void thunk(RE::BSLightingShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags)
	{
		const uint32_t originalExtraFlags = shader->currentRawTechnique & 0b111000u;

		if ((shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr)) != 0) {
			shader->currentRawTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AmbientSpecular);
		}

		shader->currentRawTechnique &= ~0b111000u;
		shader->currentRawTechnique |= ((pass->numLights - 1) << 3);

		func(shader, pass, renderFlags);

		shader->currentRawTechnique &= ~0b111000u;
		shader->currentRawTechnique |= originalExtraFlags;

		if ((shader->currentRawTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::TruePbr)) != 0) {
			shader->currentRawTechnique &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::AmbientSpecular);
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

uint32_t hk_BSLightingShader_GetPixelTechnique(uint32_t rawTechnique)
{
	uint32_t pixelTechnique = rawTechnique;

	pixelTechnique &= ~0b111000000u;
	if ((pixelTechnique & static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::ModelSpaceNormals)) == 0) {
		pixelTechnique &= ~static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::Skinned);
	}
	pixelTechnique |= static_cast<uint32_t>(SIE::ShaderCache::LightingShaderFlags::VC);

	return pixelTechnique;
}

void SetupLandscapeTexture(BSLightingShaderMaterialPBRLandscape& material, RE::TESLandTexture& landTexture, uint32_t textureIndex)
{
	if (textureIndex >= 6) {
		return;
	}

	auto textureSet = landTexture.textureSet;
	if (textureSet == nullptr) {
		return;
	}

	auto* textureSetData = TruePBR::GetSingleton()->GetPBRTextureSetData(landTexture.textureSet);
	const bool isPbr = textureSetData != nullptr;

	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::BaseColorTexture, material.landscapeBaseColorTextures[textureIndex]);
	textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::NormalTexture, material.landscapeNormalTextures[textureIndex]);

	if (isPbr) {
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::RmaosTexture, material.landscapeRMAOSTextures[textureIndex]);
		textureSet->SetTexture(BSLightingShaderMaterialPBRLandscape::DisplacementTexture, material.landscapeDisplacementTextures[textureIndex]);
		material.displacementScales[textureIndex] = textureSetData->displacementScale;
		material.roughnessScales[textureIndex] = textureSetData->roughnessScale;
		material.specularLevels[textureIndex] = textureSetData->specularLevel;
	}
	material.isPbr[textureIndex] = isPbr;

	if (material.landscapeBaseColorTextures[textureIndex] != nullptr) {
		material.numLandscapeTextures = std::max(material.numLandscapeTextures, textureIndex + 1);
	}
}

bool hk_TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
decltype(&hk_TESObjectLAND_SetupMaterial) ptr_TESObjectLAND_SetupMaterial;

bool hk_TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land)
{
	auto* singleton = TruePBR::GetSingleton();

	bool isPbr = false;
	if (land->loadedData != nullptr) {
		for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
			if (land->loadedData->defQuadTextures[quadIndex] != nullptr) {
				if (singleton->IsPBRTextureSet(land->loadedData->defQuadTextures[quadIndex]->textureSet)) {
					isPbr = true;
					break;
				}
			}
			for (uint32_t textureIndex = 0; textureIndex < 6; ++textureIndex) {
				if (land->loadedData->quadTextures[quadIndex][textureIndex] != nullptr) {
					if (singleton->IsPBRTextureSet(land->loadedData->quadTextures[quadIndex][textureIndex]->textureSet)) {
						isPbr = true;
						break;
					}
				}
			}
		}
	}

	if (!isPbr) {
		return ptr_TESObjectLAND_SetupMaterial(land);
	}

	static const auto settings = RE::INISettingCollection::GetSingleton();
	static const bool bEnableLandFade = settings->GetSetting("bEnableLandFade:Display");
	static const bool bDrawLandShadows = settings->GetSetting("bDrawLandShadows:Display");

	if (land->loadedData != nullptr && land->loadedData->mesh[0] != nullptr) {
		land->data.flags.set(static_cast<RE::OBJ_LAND::Flag>(8));
		for (uint32_t quadIndex = 0; quadIndex < 4; ++quadIndex) {
			auto shaderProperty = static_cast<RE::BSLightingShaderProperty*>(RE::MemoryManager::GetSingleton()->Allocate(sizeof(RE::BSLightingShaderProperty), 0, false));
			shaderProperty->Ctor();

			{
				BSLightingShaderMaterialPBRLandscape srcMaterial;
				shaderProperty->LinkMaterial(&srcMaterial, true);
			}

			auto material = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderProperty->material);
			const auto& stateData = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();

			for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles; ++textureIndex) {
				material->landscapeBaseColorTextures[textureIndex] = stateData.defaultTextureBlack;
				material->landscapeNormalTextures[textureIndex] = stateData.defaultTextureNormalMap;
				material->landscapeDisplacementTextures[textureIndex] = stateData.defaultTextureBlack;
				material->landscapeRMAOSTextures[textureIndex] = stateData.defaultTextureWhite;
			}

			if (auto defTexture = land->loadedData->defQuadTextures[quadIndex]) {
				SetupLandscapeTexture(*material, *defTexture, 0);
			}
			for (uint32_t textureIndex = 0; textureIndex < BSLightingShaderMaterialPBRLandscape::NumTiles - 1; ++textureIndex) {
				if (auto landTexture = land->loadedData->quadTextures[quadIndex][textureIndex]) {
					SetupLandscapeTexture(*material, *landTexture, textureIndex + 1);
				}
			}

			if (bEnableLandFade) {
				shaderProperty->unk108 = false;
			}

			bool noLODLandBlend = false;
			auto tes = RE::TES::GetSingleton();
			auto worldSpace = tes->GetRuntimeData2().worldSpace;
			if (worldSpace != nullptr) {
				if (auto terrainManager = worldSpace->GetTerrainManager()) {
					noLODLandBlend = reinterpret_cast<bool*>(terrainManager)[0x36];
				}
			}
			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kMultiTextureLandscape, true);
			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kReceiveShadows, true);
			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kCastShadows, bDrawLandShadows);
			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kNoLODLandBlend, noLODLandBlend);

			shaderProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kVertexLighting, true);

			const auto& children = land->loadedData->mesh[quadIndex]->GetChildren();
			auto geometry = children.empty() ? nullptr : static_cast<RE::BSGeometry*>(children[0].get());
			shaderProperty->SetupGeometry(geometry);
			if (geometry != nullptr) {
				geometry->GetGeometryRuntimeData().properties[1] = RE::NiPointer(shaderProperty);
			}

			RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]->AttachObject(geometry);
		}

		return true;
	}

	return false;
}

struct TESForm_GetFormEditorID
{
	static const char* thunk(const RE::TESForm* form)
	{
		auto* singleton = TruePBR::GetSingleton();
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
		auto* singleton = TruePBR::GetSingleton();
		singleton->editorIDs[form->GetFormID()] = editorId;
		return true;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void hk_SetPerFrameBuffers(void* renderer);
decltype(&hk_SetPerFrameBuffers) ptr_SetPerFrameBuffers;

void hk_SetPerFrameBuffers(void* renderer)
{
	ptr_SetPerFrameBuffers(renderer);
	TruePBR::GetSingleton()->SetupFrame();
}

void hk_BSTempEffectSimpleDecal_SetupGeometry(RE::BSTempEffectSimpleDecal* decal, RE::BSGeometry* geometry, RE::BGSTextureSet* textureSet, bool blended);
decltype(&hk_BSTempEffectSimpleDecal_SetupGeometry) ptr_BSTempEffectSimpleDecal_SetupGeometry;

void hk_BSTempEffectSimpleDecal_SetupGeometry(RE::BSTempEffectSimpleDecal* decal, RE::BSGeometry* geometry, RE::BGSTextureSet* textureSet, bool blended)
{
	ptr_BSTempEffectSimpleDecal_SetupGeometry(decal, geometry, textureSet, blended);

	if (auto* shaderProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().properties[1].get());
		shaderProperty != nullptr && TruePBR::GetSingleton()->IsPBRTextureSet(textureSet)) {
		{
			BSLightingShaderMaterialPBR srcMaterial;
			shaderProperty->LinkMaterial(&srcMaterial, true);
		}

		auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material);
		pbrMaterial->OnLoadTextureSet(0, textureSet);

		constexpr static RE::NiColor whiteColor(1.f, 1.f, 1.f);
		*shaderProperty->emissiveColor = whiteColor;
		const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
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

struct BSTempEffectGeometryDecal_Initialize
{
	static void thunk(RE::BSTempEffectGeometryDecal* decal)
	{
		func(decal);

		if (decal->decal != nullptr && TruePBR::GetSingleton()->IsPBRTextureSet(decal->texSet)) {
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
			const bool hasEmissive = pbrMaterial->emissiveTexture != nullptr && pbrMaterial->emissiveTexture != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureBlack;
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

struct BSGrassShaderProperty_ctor
{
	static RE::BSLightingShaderProperty* thunk(RE::BSLightingShaderProperty* property)
	{
		const uint64_t stackPointer = reinterpret_cast<uint64_t>(_AddressOfReturnAddress());
		const uint64_t lightingPropertyAddress = stackPointer + (REL::Module::IsAE() ? 0x68 : 0x70);
		auto* lightingProperty = *reinterpret_cast<RE::BSLightingShaderProperty**>(lightingPropertyAddress);

		RE::BSLightingShaderProperty* grassProperty = func(property);

		if (lightingProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
			if (auto* pbrSrcMaterial = static_cast<BSLightingShaderMaterialPBR*>(lightingProperty->material)) {
				BSLightingShaderMaterialPBR srcMaterial;
				grassProperty->LinkMaterial(&srcMaterial, true);

				grassProperty->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kMenuScreen, true);

				auto pbrMaterial = static_cast<BSLightingShaderMaterialPBR*>(grassProperty->material);
				pbrMaterial->pbrFlags = pbrSrcMaterial->pbrFlags;
				pbrMaterial->normalTexture = pbrSrcMaterial->normalTexture;
				pbrMaterial->rmaosTexture = pbrSrcMaterial->rmaosTexture;
				pbrMaterial->featuresTexture0 = pbrSrcMaterial->featuresTexture0;
				pbrMaterial->featuresTexture1 = pbrSrcMaterial->featuresTexture1;
				pbrMaterial->specularColorScale = pbrSrcMaterial->specularColorScale;
				pbrMaterial->specularPower = pbrSrcMaterial->specularPower;
				pbrMaterial->specularColor = pbrSrcMaterial->specularColor;
				pbrMaterial->subSurfaceLightRolloff = pbrSrcMaterial->subSurfaceLightRolloff;
				pbrMaterial->coatRoughness = pbrSrcMaterial->coatRoughness;
			}
		}

		return grassProperty;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSGrassShaderProperty_GetRenderPasses
{
	static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSLightingShaderProperty* property, RE::BSGeometry* geometry, std::uint32_t renderFlags, RE::BSShaderAccumulator* accumulator)
	{
		auto renderPasses = func(property, geometry, renderFlags, accumulator);
		if (renderPasses == nullptr) {
			return renderPasses;
		}

		const bool isPbr = property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
		if (isPbr) {
			auto currentPass = renderPasses->head;
			while (currentPass != nullptr) {
				if (currentPass->shader->shaderType == RE::BSShader::Type::Grass && currentPass->passEnum != 0x5C00005C) {
					currentPass->passEnum = 0x5C000042;
				}
				currentPass = currentPass->next;
			}
		}

		return renderPasses;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSGrassShader_SetupTechnique
{
	static bool thunk(RE::BSShader* shader, uint32_t globalTechnique)
	{
		if (globalTechnique == 0x5C000042) {
			auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			auto* graphicsState = RE::BSGraphics::State::GetSingleton();
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();

			const uint32_t localTechnique = static_cast<uint32_t>(SIE::ShaderCache::GrassShaderTechniques::TruePbr);
			uint32_t shaderDescriptor = localTechnique;
			if (graphicsState->useEarlyZ) {
				shaderDescriptor |= static_cast<uint32_t>(SIE::ShaderCache::GrassShaderFlags::AlphaTest);
			}

			const bool began = Hooks::hk_BSShader_BeginTechnique(shader, shaderDescriptor, shaderDescriptor, false);
			if (!began) {
				return false;
			}

			static auto fogMethod = REL::Relocation<void (*)()>(REL::RelocationID(100000, 106707));
			fogMethod();

			static auto* bShadowsOnGrass = RE::GetINISetting("bShadowsOnGrass:Display");
			if (!bShadowsOnGrass->GetBool()) {
				shadowState->SetPSTexture(1, graphicsState->GetRuntimeData().defaultTextureWhite->rendererTexture);
				shadowState->SetPSTextureAddressMode(1, RE::BSGraphics::TextureAddressMode::kClampSClampT);
				shadowState->SetPSTextureFilterMode(1, RE::BSGraphics::TextureFilterMode::kNearest);
			} else {
				shadowState->SetPSTexture(1, renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kSHADOW_MASK]);
				shadowState->SetPSTextureAddressMode(1, RE::BSGraphics::TextureAddressMode::kClampSClampT);

				static auto* shadowMaskQuarter = RE::GetINISetting("iShadowMaskQuarter:Display");
				shadowState->SetPSTextureFilterMode(1, shadowMaskQuarter->GetSInt() != 4 ? RE::BSGraphics::TextureFilterMode::kBilinear : RE::BSGraphics::TextureFilterMode::kNearest);
			}

			return true;
		}

		return func(shader, globalTechnique);
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

struct BSGrassShader_SetupMaterial
{
	static void thunk(RE::BSShader* shader, RE::BSLightingShaderMaterialBase const* material)
	{
		const auto& state = State::GetSingleton();
		const auto technique = static_cast<SIE::ShaderCache::GrassShaderTechniques>(state->currentPixelDescriptor & 0b1111);

		if (technique == SIE::ShaderCache::GrassShaderTechniques::TruePbr) {
			auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();

			RE::BSGraphics::Renderer::PreparePSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);

			auto* pbrMaterial = static_cast<const BSLightingShaderMaterialPBR*>(material);
			shadowState->SetPSTexture(0, pbrMaterial->diffuseTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(0, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(0, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			shadowState->SetPSTexture(2, pbrMaterial->normalTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(2, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(2, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			shadowState->SetPSTexture(3, pbrMaterial->rmaosTexture->rendererTexture);
			shadowState->SetPSTextureAddressMode(3, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
			shadowState->SetPSTextureFilterMode(3, RE::BSGraphics::TextureFilterMode::kAnisotropic);

			stl::enumeration<PBRShaderFlags> shaderFlags;
			if (pbrMaterial->pbrFlags.any(PBRFlags::Subsurface)) {
				shaderFlags.set(PBRShaderFlags::Subsurface);
			}

			const bool hasSubsurface = pbrMaterial->featuresTexture0 != nullptr && pbrMaterial->featuresTexture0 != RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureWhite;
			if (hasSubsurface) {
				shadowState->SetPSTexture(4, pbrMaterial->featuresTexture0->rendererTexture);
				shadowState->SetPSTextureAddressMode(4, static_cast<RE::BSGraphics::TextureAddressMode>(pbrMaterial->textureClampMode));
				shadowState->SetPSTextureFilterMode(4, RE::BSGraphics::TextureFilterMode::kAnisotropic);

				shaderFlags.set(PBRShaderFlags::HasFeaturesTexture0);
			}

			{
				shadowState->SetPSConstant(shaderFlags, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 0);
			}

			{
				std::array<float, 3> PBRParams1;
				PBRParams1[0] = pbrMaterial->GetRoughnessScale();
				PBRParams1[1] = pbrMaterial->GetSpecularLevel();
				shadowState->SetPSConstant(PBRParams1, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 1);
			}

			{
				std::array<float, 4> PBRParams2;
				PBRParams2[0] = pbrMaterial->GetSubsurfaceColor().red;
				PBRParams2[1] = pbrMaterial->GetSubsurfaceColor().green;
				PBRParams2[2] = pbrMaterial->GetSubsurfaceColor().blue;
				PBRParams2[3] = pbrMaterial->GetSubsurfaceOpacity();
				shadowState->SetPSConstant(PBRParams2, RE::BSGraphics::ConstantGroupLevel::PerMaterial, 2);
			}

			RE::BSGraphics::Renderer::FlushPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
			RE::BSGraphics::Renderer::ApplyPSConstantGroup(RE::BSGraphics::ConstantGroupLevel::PerMaterial);
		} else {
			func(shader, material);
		}
	};
	static inline REL::Relocation<decltype(thunk)> func;
};

struct TESBoundObject_Clone3D
{
	static RE::NiAVObject* thunk(RE::TESBoundObject* object, RE::TESObjectREFR* ref, bool arg3)
	{
		auto* result = func(object, ref, arg3);
		if (result != nullptr && ref != nullptr && ref->data.objectReference != nullptr && ref->data.objectReference->formType == RE::FormType::Static) {
			auto* stat = static_cast<RE::TESObjectSTAT*>(ref->data.objectReference);
			if (stat->data.materialObj != nullptr && stat->data.materialObj->directionalData.singlePass) {
				if (auto* pbrData = TruePBR::GetSingleton()->GetPBRMaterialObjectData(stat->data.materialObj)) {
					RE::BSVisit::TraverseScenegraphGeometries(result, [pbrData](RE::BSGeometry* geometry) {
						if (auto* shaderProperty = static_cast<RE::BSShaderProperty*>(geometry->GetGeometryRuntimeData().properties[1].get())) {
							if (shaderProperty->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting &&
								shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
								if (auto* material = static_cast<BSLightingShaderMaterialPBR*>(shaderProperty->material)) {
									material->projectedMaterialBaseColorScale = pbrData->baseColorScale;
									material->projectedMaterialRoughness = pbrData->roughness;
									material->projectedMaterialSpecularLevel = pbrData->specularLevel;
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

void TruePBR::PostPostLoad()
{
	logger::info("Hooking BSLightingShaderProperty");
	stl::write_vfunc<0x18, BSLightingShaderProperty_LoadBinary>(RE::VTABLE_BSLightingShaderProperty[0]);
	stl::write_vfunc<0x2A, BSLightingShaderProperty_GetRenderPasses>(RE::VTABLE_BSLightingShaderProperty[0]);

	logger::info("Hooking BSLightingShader");
	stl::write_vfunc<0x4, BSLightingShader_SetupMaterial>(RE::VTABLE_BSLightingShader[0]);
	stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);
	std::ignore = Detours::X64::DetourFunction(REL::RelocationID(101633, 108700).address(), (uintptr_t)&hk_BSLightingShader_GetPixelTechnique);

	logger::info("Hooking TESObjectLAND");
	*(uintptr_t*)&ptr_TESObjectLAND_SetupMaterial = Detours::X64::DetourFunction(REL::RelocationID(18368, 18791).address(), (uintptr_t)&hk_TESObjectLAND_SetupMaterial);

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

	logger::info("Hooking SetPerFrameBuffers");
	*(uintptr_t*)&ptr_SetPerFrameBuffers = Detours::X64::DetourFunction(REL::RelocationID(75570, 77371).address(), (uintptr_t)&hk_SetPerFrameBuffers);

	logger::info("Hooking BSTempEffectSimpleDecal");
	*(uintptr_t*)&ptr_BSTempEffectSimpleDecal_SetupGeometry = Detours::X64::DetourFunction(REL::RelocationID(29253, 30108).address(), (uintptr_t)&hk_BSTempEffectSimpleDecal_SetupGeometry);

	logger::info("Hooking BSTempEffectGeometryDecal");
	stl::write_vfunc<0x25, BSTempEffectGeometryDecal_Initialize>(RE::VTABLE_BSTempEffectGeometryDecal[0]);

	logger::info("Hooking BSGrassShaderProperty::ctor");
	stl::write_thunk_call<BSGrassShaderProperty_ctor>(REL::RelocationID(15214, 15383).address() + REL::Relocate(0x45B, 0x4F5));

	logger::info("Hooking BSGrassShaderProperty");
	stl::write_vfunc<0x2A, BSGrassShaderProperty_GetRenderPasses>(RE::VTABLE_BSGrassShaderProperty[0]);

	logger::info("Hooking BSGrassShader");
	stl::write_vfunc<0x2, BSGrassShader_SetupTechnique>(RE::VTABLE_BSGrassShader[0]);
	stl::write_vfunc<0x4, BSGrassShader_SetupMaterial>(RE::VTABLE_BSGrassShader[0]);

	logger::info("Hooking TESObjectSTAT");
	stl::write_vfunc<0x4A, TESBoundObject_Clone3D>(RE::VTABLE_TESObjectSTAT[0]);
}

void TruePBR::SetShaderResouces()
{
	auto context = State::GetSingleton()->context;
	for (uint32_t textureIndex = 0; textureIndex < ExtendedRendererState::NumPSTextures; ++textureIndex) {
		if (extendedRendererState.PSResourceModifiedBits & (1 << textureIndex)) {
			context->PSSetShaderResources(ExtendedRendererState::FirstPSTexture + textureIndex, 1, &extendedRendererState.PSTexture[textureIndex]);
		}
	}
	extendedRendererState.PSResourceModifiedBits = 0;
}