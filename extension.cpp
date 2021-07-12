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
#include <iplayerinfo.h>

SSF g_SSF;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_SSF);

CGlobalVars *gpGlobals = NULL;

CDetour *g_Detour_CBaseClient__SendSnapshot = NULL;

ConVar *g_SvSSFLog = CreateConVar("sv_ssf_log", "0", FCVAR_NOTIFY, "Log ssf debug print statements.");

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

	g_Detour_CBaseClient__SendSnapshot = DETOUR_CREATE_MEMBER(CBaseClient__SendSnapshot, "CBaseClient__SendSnapshot");
	if(!g_Detour_CBaseClient__SendSnapshot)
	{
		snprintf(error, maxlen, "Failed to detour CBaseClient__SendSnapshot.\n");
		return false;
	}
	g_Detour_CBaseClient__SendSnapshot->EnableDetour();

	AutoExecConfig(g_pCVar, true);

	return true;
}

void SSF::SDK_OnUnload()
{
	if(g_Detour_CBaseClient__SendSnapshot)
	{
		g_Detour_CBaseClient__SendSnapshot->Destroy();
		g_Detour_CBaseClient__SendSnapshot = NULL;
	}

	gameconfs->CloseGameConfigFile(g_pGameConf);
}

void SSF::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);

	iserver = g_pSDKTools->GetIServer();
	if (!iserver) {
		smutils->LogError(myself, "Failed to get IServer interface from SDKTools!");
		return;
	}
}

DETOUR_DECL_MEMBER1(CBaseClient__SendSnapshot, void, CClientFrame *, pFrame)
{
	if ()
	return DETOUR_MEMBER_CALL(CBaseClient__SendSnapshot)();
}

bool SSF::RegisterConCommandBase(ConCommandBase *pVar)
{
	/* Always call META_REGCVAR instead of going through the engine. */
    return META_REGCVAR(pVar);
}