#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace AddressSetter
{
	static uint32_t gCurrentExeVersion;
	static uint32_t gBaseAddress;
	static bool bAddressesRead = false;

	static INI<>* gLoadedConfig = nullptr;

	// All config values, resolved once by Init() into a read-only table. Lookups never touch the INI parser
	// afterwards: INI::get(section, key) selects the section through a shared pointer, so concurrent lookups
	// (game and render thread) could read a key from the wrong section, and parsing on every call is slow.
	// Lookups hash the section/key C strings directly (no std::string temporaries, no allocation) and probe a
	// flat open-addressing table: hundreds of call sites resolve their address on every call (e.g. per draw).
	struct ConfigValue
	{
		std::string section;
		std::string key;
		std::string text;		// raw value from the config file
		uint32_t value = 0;		// parsed address (patterns: resolved on first use)
		bool isPattern = false;
		bool resolved = false;
	};
	struct ConfigSlot
	{
		uint32_t hash;
		ConfigValue* value;		// nullptr = empty slot
	};
	static std::vector<ConfigValue> gConfigValues;	// storage (not resized after the table is built)
	static std::vector<ConfigSlot> gConfigTable;	// power-of-two sized, at most 50% full
	static SRWLOCK gPatternLock = SRWLOCK_INIT;

	static inline uint32_t HashConfigKey(const char* section, const char* key)
	{
		// FNV-1a over "section\x1Fkey"
		uint32_t h = 2166136261u;
		for (; *section; ++section) { h ^= (uint8_t)*section; h *= 16777619u; }
		h ^= 0x1Fu; h *= 16777619u;
		for (; *key; ++key) { h ^= (uint8_t)*key; h *= 16777619u; }
		return h;
	}
	static inline void BuildConfigCache()
	{
		size_t count = 0;
		for (auto& section : gLoadedConfig->sections)
			if (section.second)
				count += section.second->size();

		gConfigValues.clear();
		gConfigValues.reserve(count);

		for (auto& section : gLoadedConfig->sections)
		{
			if (!section.second)
				continue;

			for (auto& entry : *section.second)
			{
				ConfigValue v;
				v.section = section.first;
				v.key = entry.first;
				v.text = entry.second;
				v.isPattern = !(v.text.rfind("0x", 0) == 0 || v.text.rfind("0X", 0) == 0);

				if (!v.isPattern)
				{
					v.value = static_cast<uint32_t>(std::stoul(v.text, nullptr, 16));
					v.resolved = true;
				}

				gConfigValues.push_back(std::move(v));
			}
		}

		size_t tableSize = 16;
		while (tableSize < count * 2)
			tableSize <<= 1;

		gConfigTable.assign(tableSize, ConfigSlot{ 0, nullptr });
		const size_t mask = tableSize - 1;

		for (ConfigValue& v : gConfigValues)
		{
			uint32_t h = HashConfigKey(v.section.c_str(), v.key.c_str());
			size_t i = h & mask;
			while (gConfigTable[i].value)
				i = (i + 1) & mask;
			gConfigTable[i] = ConfigSlot{ h, &v };
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

	static inline ConfigValue* FindConfigValue(const char* section, const char* key)
	{
		if (!bAddressesRead)
			Init();

		if (gConfigTable.empty())
			return nullptr;

		const uint32_t h = HashConfigKey(section, key);
		const size_t mask = gConfigTable.size() - 1;

		for (size_t i = h & mask; ; i = (i + 1) & mask)
		{
			const ConfigSlot& slot = gConfigTable[i];
			if (!slot.value)
				return nullptr;
			if (slot.hash == h && slot.value->section == section && slot.value->key == key)
				return slot.value;
		}
	}

	static inline bool IsConfigPattern(const char* section, const char* key)
	{
		const ConfigValue* v = FindConfigValue(section, key);
		return v && v->isPattern;
	}
	static inline bool IsConfigPattern(const std::string& section, const std::string& key)
	{
		return IsConfigPattern(section.c_str(), key.c_str());
	}
	static inline std::string GetConfigString(const char* section, const char* key)
	{
		const ConfigValue* v = FindConfigValue(section, key);
		return v ? v->text : std::string();
	}
	static inline std::string GetConfigString(const std::string& section, const std::string& key)
	{
		return GetConfigString(section.c_str(), key.c_str());
	}
	static inline uint32_t GetAddressFromConfig(const char* section, const char* key)
	{
		ConfigValue* v = FindConfigValue(section, key);

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
	static inline uint32_t GetAddressFromConfig(const std::string& section, const std::string& key)
	{
		return GetAddressFromConfig(section.c_str(), key.c_str());
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
	static inline T& GetRef(const char* section, const char* key)
	{
		return GetRef<T>(GetAddressFromConfig(section, key));
	}
	template<typename T>
	static inline T& GetRef(const std::string& section, const std::string& key)
	{
		return GetRef<T>(GetAddressFromConfig(section.c_str(), key.c_str()));
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
	static inline uint32_t Get(const char* section, const char* key)
	{
		return Get(GetAddressFromConfig(section, key));
	}
	static inline uint32_t Get(const std::string& section, const std::string& key)
	{
		return Get(GetAddressFromConfig(section.c_str(), key.c_str()));
	}

}