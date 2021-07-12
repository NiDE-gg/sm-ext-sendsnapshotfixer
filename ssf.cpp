#include "ssf.h"

using namespace SourceHook;

SSF g_SSF;

PLUGIN_EXPOSE(SSF, g_SSF);

CGlobalVars *gpGlobals = NULL;

uint8_t g_UserIDtoClientMap[USHRT_MAX + 1];

// SH_DECL_HOOK1(IClient, SendSnapshot, SH_NOATTRIB, 0, void, CClientFrame *);

SH_DECL_MANUALHOOK1(CBaseClient_SendSnapshot, 0, 0, 0, void, CClientFrame *)

#if !defined ORANGEBOX_BUILD
ICvar* g_pCVar = NULL;
#endif

ICvar* GetICVar()
{
#if defined METAMOD_PLAPI_VERSION
#if SOURCE_ENGINE==SE_ORANGEBOX || SOURCE_ENGINE==SE_LEFT4DEAD || SOURCE_ENGINE==SE_LEFT4DEAD2 || SOURCE_ENGINE==SE_TF2 || SOURCE_ENGINE==SE_DODS || SOURCE_ENGINE==SE_HL2DM || SOURCE_ENGINE==SE_NUCLEARDAWN || \
    SOURCE_ENGINE==SE_ALIENSWARM || SOURCE_ENGINE==SE_BLOODYGOODTIME || SOURCE_ENGINE==SE_CSGO || SOURCE_ENGINE==SE_CSS || SOURCE_ENGINE==SE_INSURGENCY || SOURCE_ENGINE==SE_SDK2013 || SOURCE_ENGINE== SE_BMS
    return (ICvar *)((g_SMAPI->GetEngineFactory())(CVAR_INTERFACE_VERSION, NULL));
#else
    return (ICvar *)((g_SMAPI->GetEngineFactory())(VENGINE_CVAR_INTERFACE_VERSION, NULL));
#endif
#else
    return (ICvar *)((g_SMAPI->engineFactory())(VENGINE_CVAR_INTERFACE_VERSION, NULL));
#endif
}

ConVar cvar_ssf_log("ssf_log", "0", FCVAR_NONE, "Log sendsnapshot");

bool SSF::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	gpGlobals = ismm->GetCGlobals();

	META_LOG(g_PLAPI, "Starting plugin.");

#if SOURCE_ENGINE==SE_ORANGEBOX || SOURCE_ENGINE==SE_LEFT4DEAD || SOURCE_ENGINE==SE_LEFT4DEAD2 || SOURCE_ENGINE==SE_TF2 || SOURCE_ENGINE==SE_DODS || SOURCE_ENGINE==SE_HL2DM || SOURCE_ENGINE==SE_NUCLEARDAWN || \
    SOURCE_ENGINE==SE_ALIENSWARM || SOURCE_ENGINE==SE_BLOODYGOODTIME || SOURCE_ENGINE==SE_CSGO || SOURCE_ENGINE==SE_CSS || SOURCE_ENGINE==SE_INSURGENCY || SOURCE_ENGINE==SE_SDK2013 || SOURCE_ENGINE== SE_BMS
    g_pCVar = GetICVar();
    ConVar_Register(0, this);
#else
    ConCommandBaseMgr::OneTimeInit(this);
#endif

	return true;
}

bool SSF::Unload(char *error, size_t maxlen)
{
	return true;
}

void Hook_SendSnapshot(CClientFrame *pFrame)
{
	if (cvar_ssf_log.GetBool())
	{
		g_SMAPI->LogMsg(g_PLAPI, "SSF: Inside SendSnapshot");
	}
}

void PlayerConnect(const int client, const int userid, const bool bot, const char *name)
{
	if (client >= 1 && client <= SM_MAXPLAYERS)
	{
		g_UserIDtoClientMap[userid] = client;

		IClient* pClient = iserver->GetClient(client - 1);
		void *pGameClient = (void *)((intptr_t)pClient - 4);
		SH_ADD_MANUALHOOK(CBaseClient_SendSnapshot, pGameClient, SH_STATIC(Hook_SendSnapshot), false);
	}
}

void PlayerDisconnect(const int userid)
{
	const int client = g_UserIDtoClientMap[userid];
	g_UserIDtoClientMap[userid] = 0;

	SH_REMOVE_MANUALHOOK(CBaseClient_SendSnapshot, pGameClient, SH_STATIC(Hook_SendSnapshot), false);
}

void ConnectEvents::FireGameEvent(IGameEvent *event)
{
	const char *name = event->GetName();

	if(strcmp(name, "player_connect") == 0)
	{
		const int client = event->GetInt("index") + 1;
		const int userid = event->GetInt("userid");
		const bool bot = event->GetBool("bot");
		const char *name = event->GetString("name");
		PlayerConnect(client, userid, bot, name);
	}
	else if(strcmp(name, "player_disconnect") == 0)
	{
		const int userid = event->GetInt("userid");
		PlayerDisconnect(userid);
	}
}


bool SSF::RegisterConCommandBase(ConCommandBase *pVar)
{
    return META_REGCVAR(pVar);
}

bool SSF::Pause(char *error, size_t maxlen)
{
    return true;
}

bool SSF::Unpause(char *error, size_t maxlen)
{
    return true;
}

void SSF::AllPluginsLoaded()
{
}

const char *SSF::GetLicense()
{
	return "GPL";
}

const char *SSF::GetVersion()
{
	return "1.0.0";
}

const char *SSF::GetDate()
{
	return __DATE__;
}

const char *SSF::GetLogTag()
{
	return "SSF";
}

const char *SSF::GetAuthor()
{
	return "maxime1907";
}

const char *SSF::GetDescription()
{
	return "Fixes the currently messed up sv_parallel_sendsnapshot.";
}

const char *SSF::GetName()
{
	return "SendSnapshot Fixer";
}

const char *SSF::GetURL()
{
	return "http://www.gitlab.com/leroy_0/sendsnapshotfixer/";
}