/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"
#include "extensionHelper.h"
#include "CDetour/detours.h"
#include <sourcehook.h>
#include <iclient.h>
#include <iserver.h>
#include <igameevents.h>
#include <iplayerinfo.h>
#include <soundinfo.h>
#include <threadtools.h>

class CFrameSnapshot;
class CClientFrame;

class CBaseClient : public IGameEventListener2, public IClient
{

};

SSF g_SSF;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_SSF);

IGameConfig *g_pGameConf = NULL;
CGlobalVars *gpGlobals = NULL;

CDetour *g_Detour_CBaseServer__WriteTempEntities = NULL;
CDetour *g_Detour_CFrameSnapshot__ReleaseReference = NULL;
CDetour *g_Detour_CFrameSnapshot__CreateEmptySnapshot = NULL;

// Mutex for m_FrameSnapshots array
CThreadFastMutex									m_FrameSnapshotsWriteMutex;

ConVar *g_SvSSFLog = CreateConVar("sv_ssf_log", "0", FCVAR_NOTIFY, "Log ssf debug print statements.");
ConVar *g_sv_multiplayer_maxtempentities = CreateConVar("sv_multiplayer_maxtempentities", "64");

DETOUR_DECL_MEMBER2(CFrameSnapshot__CreateEmptySnapshot, CFrameSnapshot *, int, tickcount, int, maxEntities )
{
	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:CFrameSnapshot__CreateEmptySnapshot locking 1");
	}

	AUTO_LOCK_FM(m_FrameSnapshotsWriteMutex);
	
	CFrameSnapshot* snap = DETOUR_MEMBER_CALL(CFrameSnapshot__CreateEmptySnapshot)(tickcount, maxEntities);

	return snap;
}

// Keep list building thread-safe
// This lock was moved to to fix bug https://bugbait.valvesoftware.com/show_bug.cgi?id=53403
// Crash in CFrameSnapshotManager::GetPackedEntity where a CBaseClient's m_pBaseline snapshot could be removed the CReferencedSnapshotList destructor 
// for another client that is in WriteTempEntities

DETOUR_DECL_MEMBER0(CFrameSnapshot__ReleaseReference, void)
{
	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:CFrameSnapshot__ReleaseReference locking");
	}

	AUTO_LOCK_FM(m_FrameSnapshotsWriteMutex);
	
	DETOUR_MEMBER_CALL(CFrameSnapshot__ReleaseReference)();
}

DETOUR_DECL_MEMBER5(CBaseServer__WriteTempEntities, void, CBaseClient *, client, CFrameSnapshot *, pCurrentSnapshot, CFrameSnapshot *, pLastSnapshot, bf_write &, buf, int, ev_max)
{
	if (!client->IsHLTV() && !client->IsReplay())
	{
		if (g_SvSSFLog->GetBool())
		{
			g_pSM->LogMessage(myself, "SSF:CBaseServer__WriteTempEntities maxentities before: %d", ev_max);
		}

		// send all unreliable temp entities between last and current frame
		// send max 64 events in multi player, 255 in SP
		ev_max = client->GetServer()->IsMultiplayer() ? g_sv_multiplayer_maxtempentities->GetInt() : 255;

		if (g_SvSSFLog->GetBool())
		{
			g_pSM->LogMessage(myself, "SSF:CBaseServer__WriteTempEntities maxentities after: %d", ev_max);
		}
	}

	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:CBaseServer__WriteTempEntities locking");
	}

	AUTO_LOCK_FM(m_FrameSnapshotsWriteMutex);

	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:CBaseServer__WriteTempEntities maxentities: %d", ev_max);
	}

	DETOUR_MEMBER_CALL(CBaseServer__WriteTempEntities)(client, pCurrentSnapshot, pLastSnapshot, buf, ev_max);
}

bool SSF::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);

	gpGlobals = ismm->GetCGlobals();

    ConVar_Register(0, this);

	return true;
}

bool SSF::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	char conf_error[255] = "";
	if(!gameconfs->LoadGameConfigFile("ssf.games", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if(conf_error[0])
		{
			snprintf(error, maxlen, "Could not read ssf.games.txt: %s\n", conf_error);
		}
		return false;
	}

	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	g_Detour_CBaseServer__WriteTempEntities = DETOUR_CREATE_MEMBER(CBaseServer__WriteTempEntities, "CBaseServer__WriteTempEntities");
	if(!g_Detour_CBaseServer__WriteTempEntities)
	{
		snprintf(error, maxlen, "Failed to detour CBaseServer__WriteTempEntities.\n");
		return false;
	}
	g_Detour_CBaseServer__WriteTempEntities->EnableDetour();

	g_Detour_CFrameSnapshot__ReleaseReference = DETOUR_CREATE_MEMBER(CFrameSnapshot__ReleaseReference, "CFrameSnapshot__ReleaseReference");
	if(!g_Detour_CFrameSnapshot__ReleaseReference)
	{
		snprintf(error, maxlen, "Failed to detour CFrameSnapshot__ReleaseReference.\n");
		return false;
	}
	g_Detour_CFrameSnapshot__ReleaseReference->EnableDetour();

	g_Detour_CFrameSnapshot__CreateEmptySnapshot = DETOUR_CREATE_MEMBER(CFrameSnapshot__CreateEmptySnapshot, "CFrameSnapshot__CreateEmptySnapshot");
	if(!g_Detour_CFrameSnapshot__CreateEmptySnapshot)
	{
		snprintf(error, maxlen, "Failed to detour CFrameSnapshot__CreateEmptySnapshot.\n");
		return false;
	}
	g_Detour_CFrameSnapshot__CreateEmptySnapshot->EnableDetour();

	AutoExecConfig(g_pCVar, true);

	return true;
}

void SSF::SDK_OnUnload()
{
	if(g_Detour_CBaseServer__WriteTempEntities)
	{
		g_Detour_CBaseServer__WriteTempEntities->Destroy();
		g_Detour_CBaseServer__WriteTempEntities = NULL;
	}

	if (g_Detour_CFrameSnapshot__ReleaseReference)
	{
		g_Detour_CFrameSnapshot__ReleaseReference->Destroy();
		g_Detour_CFrameSnapshot__ReleaseReference = NULL;
	}

	if (g_Detour_CFrameSnapshot__CreateEmptySnapshot)
	{
		g_Detour_CFrameSnapshot__CreateEmptySnapshot->Destroy();
		g_Detour_CFrameSnapshot__CreateEmptySnapshot = NULL;
	}

	gameconfs->CloseGameConfigFile(g_pGameConf);
}

void SSF::SDK_OnAllLoaded()
{
}

bool SSF::RegisterConCommandBase(ConCommandBase *pVar)
{
	/* Always call META_REGCVAR instead of going through the engine. */
    return META_REGCVAR(pVar);
}