#include "pch.h"
#include "GameHooks.h"

namespace IVSDKDotNet
{
	namespace Hooking
	{

		void GameHooks::EnsureLazyHook(LazyHook which)
		{
			switch (which)
			{
				case LazyHook::AddSceneLight:	UnmanagedGameHooks::EnableAddSceneLightHook();	break;
				case LazyHook::RenderCorona:	UnmanagedGameHooks::EnableRenderCoronaHook();	break;
			}
		}

	}
}

static SRWLOCK gLazyHookLock = SRWLOCK_INIT;
static bool gAddSceneLightHookDone = false;
static bool gRenderCoronaHookDone = false;

void UnmanagedGameHooks::Initialize()
{
	// AddSceneLight and RenderCorona are not hooked here: see EnableAddSceneLightHook / EnableRenderCoronaHook.
	TryCreateHook("UnmanagedGameHooks", "GetTrafficLightState1", &GetTrafficLightState1, (void**)&originalGetTrafficLightState1);
	TryCreateHook("UnmanagedGameHooks", "GetTrafficLightState2", &GetTrafficLightState2, (void**)&originalGetTrafficLightState2);
	TryCreateHook("UnmanagedGameHooks", "RegisterNative", &RegisterNativeHook, (void**)&originalRegisterNative);
	TryCreateHook("UnmanagedGameHooks", "RegisterNativeNoChecks", &RegisterNativeNoChecksHook, (void**)&originalRegisterNativeNoChecks);
	TryCreateHook("UnmanagedGameHooks", "IsNetworkGameRunning", &IsNetworkGameRunning, (void**)&originalIsNetworkGameRunning);
	TryCreateHook("UnmanagedGameHooks", "IsThisMachineTheServer", &IsThisMachineTheServer, (void**)&originalIsThisMachineTheServer);
	TryCreateHook("UnmanagedGameHooks", "DoPickupGlow", &DoPickupGlow, (void**)&originalDoPickupGlow);

	Logger::LogDebug("[GameHooks] Done!");
}
bool UnmanagedGameHooks::EnableAddSceneLightHook()
{
	return EnableLazyHook("AddSceneLight", &AddSceneLightHook, (LPVOID*)&originalAddSceneLight, gAddSceneLightHookDone);
}
bool UnmanagedGameHooks::EnableRenderCoronaHook()
{
	return EnableLazyHook("RenderCorona", &RenderCoronaHook, (LPVOID*)&originalRenderCorona, gRenderCoronaHookDone);
}
bool UnmanagedGameHooks::EnableLazyHook(const char* key, LPVOID detour, LPVOID* ppOriginal, bool& done)
{
	AcquireSRWLockExclusive(&gLazyHookLock);

	bool ok = done;
	if (!done)
	{
		// Only attempted once; a failure is logged by TryCreateHook / below and the event simply never fires
		done = true;

		LPVOID target = nullptr;
		if (TryCreateHook("UnmanagedGameHooks", key, detour, ppOriginal, &target))
		{
			MH_STATUS mhStatus = MH_EnableHook(target);
			ok = mhStatus == MH_OK;

			if (ok)
				Logger::LogDebug(String::Format("[GameHooks] Enabled {0} hook on first subscription.", gcnew String(key)));
			else
				Logger::LogError(String::Format("[GameHooks] Could not enable {0} hook! Details: {1}", gcnew String(key), gcnew String(MH_StatusToString(mhStatus))));
		}
	}

	ReleaseSRWLockExclusive(&gLazyHookLock);
	return ok;
}
bool UnmanagedGameHooks::TryCreateHook(const std::string& section, const std::string& key, LPVOID detour, LPVOID* ppOriginal, LPVOID* pTarget)
{
	MH_STATUS mhStatus;
	LPVOID target;

	// Figure out if config is a pattern or a regular memory address
	if (AddressSetter::IsConfigPattern(section, key))
	{
		auto scan = hook::pattern(AddressSetter::GetConfigString(section, key));

		assert(!scan.empty());

		target = (LPVOID)scan.get_first(0);
	}
	else
	{
		target = (LPVOID)AddressSetter::Get(section, key);
	}

	mhStatus = MH_CreateHook(target, detour, ppOriginal);

	if (pTarget)
		*pTarget = target;

	// Validate
	if (mhStatus != MH_OK)
	{
		Logger::LogError(String::Format("[GameHooks] Could not hook {0} func! Details: {1}", gcnew String(key.c_str()), gcnew String(MH_StatusToString(mhStatus))));
		return false;
	}

	Logger::LogDebug(String::Format("[GameHooks] Hooked {0} func.", gcnew String(key.c_str())));
	return true;
}