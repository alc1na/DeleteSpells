#include "pch.h"
#include "ConfigFile.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <Windows.h>
#include <cctype>
#include <cstdio>

using namespace std;

ConfigFile& ConfigFile::GetInstance()
{
	static ConfigFile instance;
	return instance;
}

void ConfigFile::Init()
{
	GetInstance().InitImpl();
}

bool ConfigFile::GetBool(std::string_view key, bool defaultValue)
{
	auto& self = GetInstance();
	if (!self.m_Initialized) self.InitImpl();

	auto it = self.m_Variables.find(std::string(key));
	if (it == self.m_Variables.end()) return defaultValue;

	std::string val = self.Trim(it->second);
	for (char& c : val) c = (char)tolower(c);

	bool result = (val == "true" || val == "1");
	LogReadResult<bool>(std::string(key), result, defaultValue);
	return result;
}

int ConfigFile::GetInt(std::string_view key, int defaultValue)
{
	auto& self = GetInstance();
	if (!self.m_Initialized) self.InitImpl();

	auto it = self.m_Variables.find(std::string(key));
	if (it == self.m_Variables.end()) return defaultValue;

    std::string val = self.Trim(it->second);
	int result = 0;

	std::istringstream iss(val);

	// Check if the value starts with "0x" or "0X" for hex parsing
	if (val.starts_with("0x") || val.starts_with("0X"))
		iss >> std::hex >> result;
	else
		iss >> result;

	bool parsed = !iss.fail();
	LogReadResult<int>(std::string(key), parsed ? std::optional(result) : std::nullopt, defaultValue);
	return parsed ? result : defaultValue;
}

float ConfigFile::GetFloat(std::string_view key, float defaultValue)
{
	auto& self = GetInstance();
	if (!self.m_Initialized) self.InitImpl();

	auto it = self.m_Variables.find(std::string(key));
	if (it == self.m_Variables.end()) return defaultValue;

	float result = 0;
	std::istringstream iss(it->second);
	bool parsed = (iss >> result).get();
	LogReadResult<float>(std::string(key), parsed ? std::optional(result) : std::nullopt, defaultValue);
	return parsed ? result : defaultValue;
}

const std::unordered_set<uint32_t>& ConfigFile::GetBlacklistedSpells()
{
	auto& self = GetInstance();
	if (!self.m_Initialized) self.InitImpl();
	return self.m_BlacklistedFormIDs;
}

void ConfigFile::InitImpl()
{
	if (m_Initialized) return;
	m_Initialized = true;

#ifdef ASI
	const string fullPath = GetPluginDirectory() + "\\DeleteSpells.conf";
#else
	const string fullPath = GetPluginDirectory() + "\\OBSE\\Plugins\\DeleteSpells.conf";
#endif

	LoadFromFile(fullPath);
}

void ConfigFile::LoadFromFile(const std::string& fullPath)
{
	std::ifstream file(fullPath);
	if (!file.is_open()) {
		printf("[Delete Spells] Config not found, generating default...\n");
		if (!GenerateDefault(fullPath)) {
			printf("[Delete Spells] Failed to generate config file\n");
			return;
		}
		file.open(fullPath);
	}
	if (!file.is_open()) return;

	enum class ParseState { None, InArray };
	ParseState state = ParseState::None;
	std::string currentArrayName;
	std::string line;
	size_t lineNum = 0;

	while (std::getline(file, line)) {
		lineNum++;

		// Remove comment
		const auto commentPos = line.find(';');
		if (commentPos != std::string::npos) line = line.substr(0, commentPos);

		line = Trim(line);
		if (line.empty()) continue;

		// Open array block, e.g. IgnoreSpells = {
		const auto eqPos = line.find('=');
		if (eqPos != std::string::npos && line.find('{', eqPos) != std::string::npos) {
			currentArrayName = Trim(line.substr(0, eqPos));
			state = ParseState::InArray;
			continue;
		}

		// Close array
		if (line == "}") {
			state = ParseState::None;
			currentArrayName.clear();
			continue;
		}

		if (state == ParseState::InArray) {
			if (currentArrayName == "BlacklistedSpells") {
				uint32_t formID = 0;
				if (ParseHexFormID(line, formID)) {
					m_BlacklistedFormIDs.insert(formID);
				}
				else {
					printf("[Delete Spells] Invalid FormID at line %zu: %s\n", lineNum, line.c_str());
				}
			}
			continue;
		}

		// Key-value assignment
		if (eqPos != std::string::npos) {
			const std::string key = Trim(line.substr(0, eqPos));
			const std::string value = Trim(line.substr(eqPos + 1));
			m_Variables[key] = value;
		}
		else {
			printf("[Delete Spells] Invalid line at %zu: %s\n", lineNum, line.c_str());
		}
	}

	printf("[Delete Spells] Loaded %zu variables, %zu blacklisted spells\n",
		m_Variables.size(), 
		m_BlacklistedFormIDs.size());
}

bool ConfigFile::GenerateDefault(const string& path)
{
	ofstream out(path);
	if (!out.is_open()) return false;

	out << "; === ConfigFile ===\n";
	out << "bProtectSpells = true ; If true, spells in the blacklist will not be deleted\n";
	out << "bSpellInfoLog = false ; If true, spell information will be logged to the console\n";
	out << "\n";
	out << "; === Keyboard ===\n";
	out << "iKeyboardDeleteKey = 0x45 ; Default is VK_E\n";
	out << "iKeyboardModifierKey = 0xA0 ; Default is VK_LSHIFT\n";
	out << "\n";
	out << "; === Gamepad ===\n";
	out << "bGamepadSupport = true ; If true, allows gamepad combo to trigger deletion\n";
	out << "iGamepadDeleteButton = 0x1000 ; Default is XINPUT_GAMEPAD_A\n";
	out << "iGamepadModifierButton = 0x0020 ; Default is XINPUT_GAMEPAD_BACK\n";
	out << "\n";
	out << "; === Blacklist ===\n";
	out << "BlacklistedSpells = {\n";
	out << "    0x00000136 ; Heal Minor Wounds\n";
	out << "}\n";

	out.close();
	printf("[Delete Spells] Default config generated at: %s\n", path.c_str());
	return true;
}

bool ConfigFile::ParseHexFormID(const std::string& value, uint32_t& out)
{
	std::string trimmed = Trim(value);
	if (trimmed.starts_with("0x") || trimmed.starts_with("0X"))
		trimmed = trimmed.substr(2);
	std::istringstream iss(trimmed);
	iss >> std::hex >> out;
	return !iss.fail();
}

std::string ConfigFile::Trim(const std::string& str)
{
	const auto first = str.find_first_not_of(" \t\r\n");
	if (first == string::npos) return "";
	const auto last = str.find_last_not_of(" \t\r\n");
	return str.substr(first, last - first + 1);
}

std::string ConfigFile::GetPluginDirectory()
{
	HMODULE hModule = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, nullptr, &hModule);

	char path[MAX_PATH] = {};
	GetModuleFileNameA(hModule, path, MAX_PATH);
	return std::filesystem::path(path).parent_path().string();
}