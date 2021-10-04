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

SSF g_SSF;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_SSF);

IGameConfig *g_pGameConf = NULL;
CGlobalVars *gpGlobals = NULL;

CDetour *g_Detour_CBaseServer__WriteTempEntities = NULL;
CDetour *g_Detour_CFrameSnapshotManager__CreateEmptySnapshot = NULL;

Func_CFrameSnapshot__m_FrameSnapshots__AddToTail g_Func_CFrameSnapshot__m_FrameSnapshots__AddToTail;
Func_CFrameSnapshot__m_FrameSnapshots__Remove g_Func_CFrameSnapshot__m_FrameSnapshots__Remove;
// Func_CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot;
Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot;
Func_CFrameSnapshotManager__g_FrameSnapshotManager__RemoveEntityReference g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__RemoveEntityReference;

CThreadFastMutex		m_FrameSnapshotsWriteMutex;

// ConVar *g_SvSSFLog = CreateConVar("sv_ssf_log", "0", FCVAR_NOTIFY, "Log ssf debug print statements.");
ConVar *g_sv_multiplayer_maxtempentities = CreateConVar("sv_multiplayer_maxtempentities", "64");

DETOUR_DECL_MEMBER2(CFrameSnapshotManager__CreateEmptySnapshot, CFrameSnapshot *, int, tickcount, int, maxEntities )
{
	// AUTO_LOCK_FM(m_FrameSnapshotsWriteMutex);
	
	// CFrameSnapshot* snap = DETOUR_MEMBER_CALL(CFrameSnapshotManager__CreateEmptySnapshot)(tickcount, maxEntities);

	CFrameSnapshot *snap = NULL;
	{
//		AUTO_LOCK_FM( m_FrameSnapshotsWriteMutex );
		snap = new CFrameSnapshot;
		snap->AddReference();
	}

	snap->m_nTickCount = tickcount;
	snap->m_nNumEntities = maxEntities;
	snap->m_nValidEntities = 0;
	snap->m_pValidEntities = NULL;
	snap->m_pHLTVEntityData = NULL;
	snap->m_pReplayEntityData = NULL;
	snap->m_pEntities = new CFrameSnapshotEntry[maxEntities];

	CFrameSnapshotEntry *entry = snap->m_pEntities;
	
	// clear entries
	for ( int i=0; i < maxEntities; i++)
	{
		entry->m_pClass = NULL;
		entry->m_nSerialNumber = -1;
		entry->m_pPackedData = INVALID_PACKED_ENTITY_HANDLE;
		entry++;
	}

	{
//		AUTO_LOCK_FM( m_FrameSnapshotsWriteMutex );
		g_pSM->LogMessage(myself, "Before g_Func_CFrameSnapshot__m_FrameSnapshots__AddToTail");

		snap->m_ListIndex = g_Func_CFrameSnapshot__m_FrameSnapshots__AddToTail( snap );

		g_pSM->LogMessage(myself, "After g_Func_CFrameSnapshot__m_FrameSnapshots__AddToTail");
	}

	return snap;
}

DETOUR_DECL_MEMBER5(CBaseServer__WriteTempEntities, void, CBaseClient *, client, CFrameSnapshot *, pCurrentSnapshot, CFrameSnapshot *, pLastSnapshot, bf_write &, buf, int, ev_max)
{
	if (!client->IsHLTV() && !client->IsReplay())
	{
		// send all unreliable temp entities between last and current frame
		// send max 64 events in multi player, 255 in SP
		ev_max = client->GetServer()->IsMultiplayer() ? g_sv_multiplayer_maxtempentities->GetInt() : 255;
	}

	AUTO_LOCK_FM(m_FrameSnapshotsWriteMutex);

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

	g_Detour_CFrameSnapshotManager__CreateEmptySnapshot = DETOUR_CREATE_MEMBER(CFrameSnapshotManager__CreateEmptySnapshot, "CFrameSnapshotManager__CreateEmptySnapshot");
	if(!g_Detour_CFrameSnapshotManager__CreateEmptySnapshot)
	{
		snprintf(error, maxlen, "Failed to detour CFrameSnapshotManager__CreateEmptySnapshot.\n");
		return false;
	}
	g_Detour_CFrameSnapshotManager__CreateEmptySnapshot->EnableDetour();

	void *address = NULL;
	if (!g_pGameConf->GetMemSig("CFrameSnapshot__m_FrameSnapshots__AddToTail", &address))
	{
		snprintf(error, maxlen, "Failed to find function CFrameSnapshot__m_FrameSnapshots__AddToTail.\n");
		return false;
	}
	Func_CFrameSnapshot__m_FrameSnapshots__AddToTail g_Func_CFrameSnapshot__m_FrameSnapshots__AddToTail = (Func_CFrameSnapshot__m_FrameSnapshots__AddToTail)address; // Pointer to the function scanned from memory

	address = NULL;
	if (!g_pGameConf->GetMemSig("CFrameSnapshot__m_FrameSnapshots__Remove", &address))
	{
		snprintf(error, maxlen, "Failed to find function CFrameSnapshot__m_FrameSnapshots__Remove.\n");
		return false;
	}
	Func_CFrameSnapshot__m_FrameSnapshots__Remove g_Func_CFrameSnapshot__m_FrameSnapshots__Remove = (Func_CFrameSnapshot__m_FrameSnapshots__Remove)address; // Pointer to the function scanned from memory

	address = NULL;
	if (!g_pGameConf->GetMemSig("CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot", &address))
	{
		snprintf(error, maxlen, "Failed to find function CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot.\n");
		return false;
	}
	Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot = (Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot)address; // Pointer to the function scanned from memory

	// address = NULL;
	// if (!g_pGameConf->GetMemSig("CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot", &address))
	// {
	// 	snprintf(error, maxlen, "Failed to find function CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot.\n");
	// 	return false;
	// }
	// Func_CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot = (Func_CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot)address; // Pointer to the function scanned from memory

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

	if (g_Detour_CFrameSnapshotManager__CreateEmptySnapshot)
	{
		g_Detour_CFrameSnapshotManager__CreateEmptySnapshot->Destroy();
		g_Detour_CFrameSnapshotManager__CreateEmptySnapshot = NULL;
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