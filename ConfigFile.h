#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <cstdint>
#include <optional>

class ConfigFile
{
public:
	static void Init(); // ручной вызов, если нужно
	static bool GetBool(std::string_view key, bool defaultValue = false);
	static int GetInt(std::string_view key, int defaultValue = 0);
	static float GetFloat(std::string_view key, float defaultValue = 0.0f);

	static const std::unordered_set<uint32_t>& GetBlacklistedSpells();

private:
	static ConfigFile& GetInstance();

	void InitImpl();
	void LoadFromFile(const std::string& fullPath);
	bool GenerateDefault(const std::string& path);

	bool ParseHexFormID(const std::string& value, uint32_t& out);
	std::string Trim(const std::string& str);
	std::string GetPluginDirectory();

	template <typename T>
	static void LogReadResult(const std::string& key, const std::optional<T>& value, const T& fallback) {
		auto toString = [](const T& v) -> std::string {
			if constexpr (std::is_same_v<T, bool>)
				return v ? "true" : "false";
			else
				return std::to_string(v);
			};

		if (value) {
			printf("[Delete Spells] Config option: %s = %s\n", key.c_str(), toString(*value).c_str());
		}
		else {
			printf("[Delete Spells] Config option: %s = <default> (%s)\n", key.c_str(), toString(fallback).c_str());
		}
	}

private:
	bool m_Initialized = false;
	std::unordered_map<std::string, std::string> m_Variables;
	std::unordered_set<uint32_t> m_BlacklistedFormIDs;
};
