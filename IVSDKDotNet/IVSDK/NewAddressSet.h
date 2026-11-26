#include <filesystem>
#include <string>
#include <unordered_map>

namespace AddressSetter
{
	static uint32_t gCurrentExeVersion;
	static uint32_t gBaseAddress;
	static bool bAddressesRead = false;

	static INI<>* gLoadedConfig = nullptr;

	// All config values, resolved once by Init() into a read-only table. Lookups never touch the INI parser
	// afterwards: INI::get(section, key) selects the section through a shared pointer, so concurrent lookups
	// (game and render thread) could read a key from the wrong section, and parsing on every call is slow.
	struct ConfigValue
	{
		std::string text;		// raw value from the config file
		uint32_t value = 0;		// parsed address (patterns: resolved on first use)
		bool isPattern = false;
		bool resolved = false;
	};
	static std::unordered_map<std::string, ConfigValue> gConfigValues;
	static SRWLOCK gPatternLock = SRWLOCK_INIT;

	static inline std::string MakeConfigKey(const std::string& section, const std::string& key)
	{
		std::string k;
		k.reserve(section.size() + key.size() + 1);
		k.append(section).push_back('\x1F');
		k.append(key);
		return k;
	}
	static inline void BuildConfigCache()
	{
		gConfigValues.clear();
		gConfigValues.reserve(1024);

		for (auto& section : gLoadedConfig->sections)
		{
			if (!section.second)
				continue;

			for (auto& entry : *section.second)
			{
				ConfigValue v;
				v.text = entry.second;
				v.isPattern = !(v.text.rfind("0x", 0) == 0 || v.text.rfind("0X", 0) == 0);

				if (!v.isPattern)
				{
					v.value = static_cast<uint32_t>(std::stoul(v.text, nullptr, 16));
					v.resolved = true;
				}

				gConfigValues.emplace(MakeConfigKey(section.first, entry.first), std::move(v));
			}
		}
	}

	static inline bool DoesFileExists(const std::string& name)
	{
		std::ifstream f(name.c_str());
		return f.good();
	}
	static inline uint32_t GetVersionFromEXE()
	{
		TCHAR szFileName[MAX_PATH];

		GetModuleFileName(NULL, szFileName, MAX_PATH);

		DWORD  verHandle = 0;
		UINT   size = 0;
		LPBYTE lpBuffer = NULL;
		DWORD  verSize = GetFileVersionInfoSize(szFileName, &verHandle);

		if (verSize != NULL)
		{
			LPSTR verData = new char[verSize];

			if (GetFileVersionInfo(szFileName, verHandle, verSize, verData))
			{
				if (VerQueryValue(verData, TEXT("\\"), (VOID FAR * FAR*) & lpBuffer, &size))
				{
					if (size)
					{
						VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
						if (verInfo->dwSignature == 0xfeef04bd)
						{
							std::string str = std::to_string((verInfo->dwFileVersionMS >> 16) & 0xffff);
							str += std::to_string((verInfo->dwFileVersionMS >> 0) & 0xffff);
							str += std::to_string((verInfo->dwFileVersionLS >> 16) & 0xffff);
							str += std::to_string((verInfo->dwFileVersionLS >> 0) & 0xffff);
							delete[] verData;
							return std::stoi(str);
						}
					}
				}
			}
			delete[] verData;
		}
		return 0;
	}

	static inline void DetermineVersion()
	{
		// Determine the game version
		gCurrentExeVersion = GetVersionFromEXE();

		// Set the game version
		plugin::gameVer = (plugin::eGameVersion)gCurrentExeVersion;
	}

	static inline bool LoadCurrentVersionConfig()
	{
		int numArgs;
		LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &numArgs);

		if (args == NULL)
		{
			MessageBoxA(NULL, "Failed to parse command line of current process!", "IVSDK", MB_ICONERROR);
			exit(1);
			return false;
		}

		std::filesystem::path rootPath =	std::filesystem::path(std::wstring(args[0])).remove_filename();
		std::string finalPath =				rootPath.append("IVSDK").append(std::to_string(gCurrentExeVersion).append(".ini")).string();

		LocalFree(args);

		if (!DoesFileExists(finalPath))
			return false;

		gLoadedConfig = new INI<>(finalPath, true);
		return true;
	}
	static inline void Init()
	{
		if (bAddressesRead)
			return;

		gBaseAddress = (uint32_t)GetModuleHandle(NULL);

		// Dertemine the game version
		DetermineVersion();

		// Class layouts are compiled in, so make sure this build matches the running game
#ifdef IVSDK_CE
		if (plugin::gameVer != plugin::VERSION_12059)
		{
			MessageBoxA(NULL, std::string("This build of IV-SDK .NET is for GTA IV Complete Edition 1.2.0.59, but the game version is ").append(std::to_string(gCurrentExeVersion)).append(".\nUse a build compiled with IVSDK_CLASSIC for 1.0.7.0 / 1.0.8.0.").c_str(), "IVSDK", MB_ICONERROR);
			exit(1);
			return;
		}
#else
		if (plugin::gameVer == plugin::VERSION_12059)
		{
			MessageBoxA(NULL, "This build of IV-SDK .NET is for GTA IV 1.0.7.0 / 1.0.8.0, but the game is Complete Edition 1.2.0.59.\nUse a build compiled without IVSDK_CLASSIC.", "IVSDK", MB_ICONERROR);
			exit(1);
			return;
		}
#endif

		// Load config file according to the game version
		if (!LoadCurrentVersionConfig())
		{
			MessageBoxA(NULL, std::string("Config file for version ").append(std::to_string(gCurrentExeVersion)).append(" was not found! IVSDK might not be compatible with this version yet.").c_str(), "IVSDK", MB_ICONERROR);
			exit(1);
			return;
		}

		BuildConfigCache();

		bAddressesRead = true;
	}

	static inline const ConfigValue* FindConfigValue(const std::string& section, const std::string& key)
	{
		if (!bAddressesRead)
			Init();

		auto it = gConfigValues.find(MakeConfigKey(section, key));
		return it == gConfigValues.end() ? nullptr : &it->second;
	}

	static inline bool IsConfigPattern(const std::string& section, const std::string& key)
	{
		const ConfigValue* v = FindConfigValue(section, key);
		return v && v->isPattern;
	}
	static inline std::string GetConfigString(const std::string& section, const std::string& key)
	{
		const ConfigValue* v = FindConfigValue(section, key);
		return v ? v->text : std::string();
	}
	static inline uint32_t GetAddressFromConfig(const std::string& section, const std::string& key)
	{
		ConfigValue* v = const_cast<ConfigValue*>(FindConfigValue(section, key));

		if (!v)
		{
			MessageBoxA(NULL, std::string("Address for section ").append(section).append(" and key ").append(key).append(" was not found in the config file for version ").append(std::to_string(gCurrentExeVersion)).append("!").c_str(), "IVSDK", MB_ICONERROR);
			exit(1);
			return 0;
		}

		if (!v->isPattern)
			return v->value;

		// Patterns are scanned once, on first use (unchanged behaviour: returns the dword at the match)
		AcquireSRWLockExclusive(&gPatternLock);
		if (!v->resolved)
		{
			auto scan = hook::pattern(v->text);

			assert(!scan.empty());

			v->value = *(uint32_t*)scan.get_first(0);
			v->resolved = true;
		}
		uint32_t value = v->value;
		ReleaseSRWLockExclusive(&gPatternLock);

		return value;
	}

	// Note that the base address is added here and 0x400000 is not subtracted, so rebase your .idb to 0x0 or subtract it yourself
	template<typename T>
	static inline T& GetRef(uint32_t addr1070, uint32_t addr1080)
	{
		if (!bAddressesRead)
			Init();

		switch(plugin::gameVer)
		{
			case plugin::VERSION_1080: return *reinterpret_cast<T*>(gBaseAddress + addr1080);
			case plugin::VERSION_1070: return *reinterpret_cast<T*>(gBaseAddress + addr1070);
		}

		return *reinterpret_cast<T*>(nullptr);
	}
	// Same as above, with an additional address for GTA IV Complete Edition 1.2.0.59
	template<typename T>
	static inline T& GetRef(uint32_t addr1070, uint32_t addr1080, uint32_t addr12059)
	{
		if (!bAddressesRead)
			Init();

		switch (plugin::gameVer)
		{
			case plugin::VERSION_12059: return *reinterpret_cast<T*>(gBaseAddress + addr12059);
			case plugin::VERSION_1080: return *reinterpret_cast<T*>(gBaseAddress + addr1080);
			case plugin::VERSION_1070: return *reinterpret_cast<T*>(gBaseAddress + addr1070);
		}

		return *reinterpret_cast<T*>(nullptr);
	}

	template<typename T>
	static inline T& GetRef(uint32_t addr)
	{
		if (!bAddressesRead)
			Init();

		return *reinterpret_cast<T*>(gBaseAddress + addr);
	}

	template<typename T>
	static inline T& GetRef(const std::string& section, const std::string& key)
	{
		return GetRef<T>(GetAddressFromConfig(section, key));
	}

	static inline uint32_t Get(uint32_t addr1070, uint32_t addr1080)
	{
		if (!bAddressesRead)
			Init();

		switch(plugin::gameVer)
		{
			case plugin::VERSION_1080: return gBaseAddress + addr1080;
			case plugin::VERSION_1070: return gBaseAddress + addr1070;
		}

		return 0;
	}
	// Same as above, with an additional address for GTA IV Complete Edition 1.2.0.59
	static inline uint32_t Get(uint32_t addr1070, uint32_t addr1080, uint32_t addr12059)
	{
		if (!bAddressesRead)
			Init();

		switch (plugin::gameVer)
		{
			case plugin::VERSION_12059: return addr12059 ? gBaseAddress + addr12059 : 0;
			case plugin::VERSION_1080: return addr1080 ? gBaseAddress + addr1080 : 0;
			case plugin::VERSION_1070: return addr1070 ? gBaseAddress + addr1070 : 0;
		}

		return 0;
	}
	static inline uint32_t Get(uint32_t addr)
	{
		if (!bAddressesRead)
			Init();

		return gBaseAddress + addr;
	}
	static inline uint32_t Get(const std::string& section, const std::string& key)
	{
		return Get(GetAddressFromConfig(section, key));
	}

}