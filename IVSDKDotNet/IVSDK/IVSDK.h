#define VALIDATE_SIZE(struc, size) static_assert(sizeof(struc) == size, "Invalid structure size of " #struc)
#define VALIDATE_OFFSET(struc, member, offset) \
	static_assert(offsetof(struc, member) == offset, "The offset of " #member " in " #struc " is not " #offset "...")

#ifndef INCLUDE_GUARD
#define INCLUDE_GUARD

// ---------------------------------------------------------------------------------------------
// Build target: GTA IV Complete Edition (1.2.0.59) vs. classic (1.0.7.0 / 1.0.8.0)
//
// Complete Edition changed the memory layout of CVehicle, CPed (incl. PedWeapons) and
// CPlayerInfo, so the target has to be chosen at compile time. This branch builds for
// Complete Edition by default. Define IVSDK_CLASSIC to build for 1.0.7.0 / 1.0.8.0 instead.
// A build refuses to start on a game version it was not compiled for (see AddressSetter::Init).
// ---------------------------------------------------------------------------------------------
#if !defined(IVSDK_CLASSIC) && !defined(IVSDK_CE)
#define IVSDK_CE
#endif

#ifdef IVSDK_CE
#define IVSDK_CE_SELECT(classic, ce) ce
#else
#define IVSDK_CE_SELECT(classic, ce) classic
#endif

// Translate a 1.0.8.0 member offset into the build target's offset (used by VALIDATE_OFFSET).
// CE: 0x50 bytes were removed from CVehicle at 0x210..0x260.
#define IVSDK_VEH_OFS(o)	IVSDK_CE_SELECT((o), ((o) >= 0x260 ? (o) - 0x50 : (o)))
// CE: PedWeapons (at CPed+0x2B0) lost its leading 0x18 bytes, 8 bytes were added before 0x484.
#define IVSDK_PED_OFS(o)	IVSDK_CE_SELECT((o), ((o) >= 0x484 ? (o) - 0x10 : ((o) >= 0x2C8 ? (o) - 0x18 : (o))))
// CE: 0x10 bytes were added at 0x48..0x4C of CPlayerInfo and 4 bytes removed at 0x4AC..0x4BC.
#define IVSDK_PLINFO_OFS(o)	IVSDK_CE_SELECT((o), ((o) >= 0x4BC ? (o) + 0xC : ((o) >= 0x4C ? (o) + 0x10 : (o))))

#pragma comment(lib, "version.lib")

namespace plugin
{
	enum eGameVersion : uint32_t
	{
		VERSION_NONE,
		VERSION_1000	= 1000,
		VERSION_1010	= 1010,
		VERSION_1020	= 1020,
		VERSION_1030	= 1030,
		VERSION_1040	= 1040,
		VERSION_1050	= 1050,
		VERSION_1060	= 1060,
		VERSION_1070	= 1070,
		VERSION_1080	= 1080,
		VERSION_12030	= 12030,
		VERSION_12031	= 12031,
		VERSION_12032	= 12032,
		VERSION_12041	= 12041,
		VERSION_12043	= 12043,
		VERSION_12059	= 12059,
	};
	static eGameVersion gameVer = VERSION_NONE;
	void gameStartupEvent();
}

#include "..\INI.h"
#include "..\Hooking.Patterns.h"

#include "NewAddressSet.h"

#include "CRGBA.h"
#include "rage.h"
#include "CRect.h"
#include "CVector.h"
#include "CVector2D.h"
#include "CVector4.h"
#include "CQuaternion.h"
#include "CMatrix.h"
#include "CPlayerInfo.h"
#include "CWorld.h"
#include "CPad.h"
#include "CSimpleTransform.h"
#include "CEntity.h"
#include "CDynamicEntity.h"
#include "CBuilding.h"
#include "CInteriorInst.h"
#include "CPhysical.h"
#include "CCustomShaderEffectBase.h"
#include "CObject.h"
#include "CPool.h"
#include "CPools.h"
#include "CTask.h"
#include "CTaskComplexWander.h"
#include "CTaskComplexDie.h"
#include "CTaskComplexNewGetInVehicle.h"
#include "CTaskComplexClimbLadder.h"
#include "CTaskComplexPlayerSettingsTask.h"
#include "CTaskComplexInWater.h"
#include "CTaskComplexMobileMakeCall.h"
#include "CTaskComplexPlayerOnFoot.h"
#include "CTaskComplexNM.h"
#include "CTaskSimpleNMJumpRollFromRoadVehicle.h"
#include "CTaskSimpleSidewaysDive.h"
#include "CTaskSimpleNMHighFall.h"
#include "CPed.h"
#include "CPedFactoryNY.h"
#include "CVehicleFactoryNY.h"
#include "CRadar.h"
#include "CGarage.h"
#include "cHandlingDataMgr.h"
#include "CVehicle.h"
#include "CModelInfo.h"
#include "phBound.h"
#include "phInstGta.h"
#include "CTheScripts.h"
#include "phConstrainedCollider.h"
#include "CTimer.h"
#include "CStreaming.h"
#include "CFileLoader.h"
#include "CdStream.h"
#include "CGame.h"
#include "CCam.h"
#include "CCamera.h"
#include "CExplosion.h"
#include "CHudColours.h"
#include "CVisibilityPlugins.h"
#include "CNetwork.h"
#include "CShadows.h"

#include "CWeaponInfo.h"

#include "CClock.h"
#include "CCutsceneMgr.h"
#include "CRenderPhase.h"
#include "grcTexture.h"
#include "grcTextureFactory.h"
#include "grcWindow.h"
#include "CSprite2d.h"
#include "CDraw.h"
#include "pgDictionary.h"
#include "CTxdStore.h"
#include "CFileMgr.h"
#include "CFont.h"
#include "audRadioAudioEntity.h"
#include "SkyDome.h"
#include "CBaseDC.h"
#include "CDrawRectDC.h"
#include "CDrawCurvedWindowDC.h"
#include "CDrawSpriteDC.h"
#include "CDrawSpriteUVDC.h"
#include "CDrawRadioHudTextDC.h"
#include "CMenuManager.h"
#include "CShaderLib.h"
#include "CTimeCycle.h"
#include "CPickups.h"

#include "fiDevice.h"
#include "fiPackfile.h"
#include "CGameConfigReader.h"

#include "audEngine.h"
#include "CPedType.h"
#include "CQuadTreeNode.h"
#include "CIplStore.h"
#include "CGameLogic.h"
#include "CStats.h"
#include "CGenericGameStorage.h"
#include "CReplay.h"
#include "CAudioZones.h"
#include "CStore.h"
#include "CAnimManager.h"
#include "CPopulation.h"
#include "CWeather.h"
#include "CAchievements.h"
#include "CProjectileInfo.h"
#include "CCheat.h"
#include "CWanted.h"
#include "CText.h"
#include "C_PcSave.h"
#include "CRestart.h"
#include "CTheZones.h"
#include "CGangs.h"
#include "CStuntJumpManager.h"
#include "CAERadioTrackManager.h"
#include "CFrontEnd.h"
#include "CEpisodes.h"

#endif