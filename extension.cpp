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

class CFrameSnapshot;
class CClientFrame;

#define NETMSG_TYPE_BITS	6	// must be 2^NETMSG_TYPE_BITS > SVC_LASTMSG

#define NET_MAX_PAYLOAD			4000

#define	svc_Sounds			17		// starts playing sound

#define DECLARE_BASE_MESSAGE( msgtype )						\
	public:													\
		bool			ReadFromBuffer( bf_read &buffer );	\
		bool			WriteToBuffer( bf_write &buffer );	\
		const char		*ToString() const;					\
		int				GetType() const { return msgtype; } \
		const char		*GetName() const { return #msgtype;}\
			
#define DECLARE_SVC_MESSAGE( name )		\
	DECLARE_BASE_MESSAGE( svc_##name );	\
	IServerMessageHandler *m_pMessageHandler;\
	bool Process() { return m_pMessageHandler->Process##name( this ); }\

class INetChannelInfo
{
public:

	enum {
		GENERIC = 0,	// must be first and is default group
		LOCALPLAYER,	// bytes for local player entity update
		OTHERPLAYERS,	// bytes for other players update
		ENTITIES,		// all other entity bytes
		SOUNDS,			// game sounds
		EVENTS,			// event messages
		USERMESSAGES,	// user messages
		ENTMESSAGES,	// entity messages
		VOICE,			// voice data
		STRINGTABLE,	// a stringtable update
		MOVE,			// client move cmds
		STRINGCMD,		// string command
		SIGNON,			// various signondata
		TOTAL,			// must be last and is not a real group
	};
};

class INetMessage
{
public:
	virtual	~INetMessage() {};

	// Use these to setup who can hear whose voice.
	// Pass in client indices (which are their ent indices - 1).
	
	virtual void	SetNetChannel(INetChannel * netchan) = 0; // netchannel this message is from/for
	virtual void	SetReliable( bool state ) = 0;	// set to true if it's a reliable message
	
	virtual bool	Process( void ) = 0; // calles the recently set handler to process this message
	
	virtual	bool	ReadFromBuffer( bf_read &buffer ) = 0; // returns true if parsing was OK
	virtual	bool	WriteToBuffer( bf_write &buffer ) = 0;	// returns true if writing was OK
		
	virtual bool	IsReliable( void ) const = 0;  // true, if message needs reliable handling
	
	virtual int				GetType( void ) const = 0; // returns module specific header tag eg svc_serverinfo
	virtual int				GetGroup( void ) const = 0;	// returns net message group of this message
	virtual const char		*GetName( void ) const = 0;	// returns network message name, eg "svc_serverinfo"
	virtual INetChannel		*GetNetChannel( void ) const = 0;
	virtual const char		*ToString( void ) const = 0; // returns a human readable string about message content
};

class CNetMessage : public INetMessage
{
public:
	CNetMessage() {	m_bReliable = true;
					m_NetChannel = NULL; }

	virtual ~CNetMessage() {};

	virtual int		GetGroup() const { return INetChannelInfo::GENERIC; }
	INetChannel		*GetNetChannel() const { return m_NetChannel; }
		
	virtual void	SetReliable( bool state) {m_bReliable = state;};
	virtual bool	IsReliable() const { return m_bReliable; };
	virtual void    SetNetChannel(INetChannel * netchan) { m_NetChannel = netchan; }	
	virtual bool	Process() { Assert( 0 ); return false; };	// no handler set

protected:
	bool				m_bReliable;	// true if message should be send reliable
	INetChannel			*m_NetChannel;	// netchannel this message is from/for
};

class SVC_Sounds : public CNetMessage
{
	DECLARE_SVC_MESSAGE( Sounds );

	int	GetGroup() const { return INetChannelInfo::SOUNDS; }

	SVC_Sounds() : CNetMessage()
	{

	}

	~SVC_Sounds()
	{

	}

public:	

	bool		m_bReliableSound;
	int			m_nNumSounds;
	int			m_nLength;
	bf_read		m_DataIn;
	bf_write	m_DataOut;
};

class CBaseClient : public IGameEventListener2, public IClient
{

};

class CGameClient: public CBaseClient
{
public:
	CUtlVector<SoundInfo_t>	m_Sounds;			// game sounds
};

bool SVC_Sounds::WriteToBuffer( bf_write &buffer )
{
	m_nLength = m_DataOut.GetNumBitsWritten();

	buffer.WriteUBitLong( GetType(), NETMSG_TYPE_BITS );

	Assert( m_nNumSounds > 0 );
	
	if ( m_bReliableSound )
	{
		// as single sound message is 32 bytes long maximum
		buffer.WriteOneBit( 1 );
		buffer.WriteUBitLong( m_nLength, 8 );
	}
	else
	{
		// a bunch of unreliable messages
		buffer.WriteOneBit( 0 );
		buffer.WriteUBitLong( m_nNumSounds, 8 );
		buffer.WriteUBitLong( m_nLength, 16  );
	}
	
	return buffer.WriteBits( m_DataOut.GetData(), m_nLength );
}

SSF g_SSF;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_SSF);

IGameConfig *g_pGameConf = NULL;
CGlobalVars *gpGlobals = NULL;

CDetour *g_Detour_CBaseClient__SendSnapshot = NULL;
CDetour *g_Detour_CBaseServer__WriteTempEntities = NULL;
CDetour *g_Detour_CGameClient__FillSoundsMessage = NULL;
CDetour *g_Detour_CGameClient__WriteGameSounds = NULL;

ConVar *g_SvSSFLog = CreateConVar("sv_ssf_log", "0", FCVAR_NOTIFY, "Log ssf debug print statements.");
ConVar *g_sv_multiplayer_maxtempentities = CreateConVar("sv_multiplayer_maxtempentities", "64");
ConVar *g_sv_multiplayer_maxsounds = CreateConVar("sv_multiplayer_sounds", "32");
ConVar *g_sv_sound_discardextraunreliable = CreateConVar( "sv_sound_discardextraunreliable", "1" );

int Custom_CGameClient__FillSoundsMessage(CGameClient *pGameClient, SVC_Sounds &msg)
{
	int nMaxSounds = pGameClient->GetServer()->IsMultiplayer() ? g_sv_multiplayer_maxsounds->GetInt() : 255;
	int i, count = pGameClient->m_Sounds.Count();

	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:CGameClient__FillSoundsMessage maxsounds before: %d, count: %d", nMaxSounds, pGameClient->m_Sounds.Count());
	}

	// Discard events if we have too many to signal with 8 bits
	if ( count > nMaxSounds )
		count = nMaxSounds;

	// Nothing to send
	if ( !count )
		return 0;

	SoundInfo_t defaultSound; defaultSound.SetDefault();
	SoundInfo_t *pDeltaSound = &defaultSound;

	msg.m_nNumSounds = count;
	msg.m_bReliableSound = false;
	msg.SetReliable( false );

	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:CGameClient__FillSoundsMessage getnumbitsleft: %d", msg.m_DataOut.GetNumBitsLeft());
	}

	Assert( msg.m_DataOut.GetNumBitsLeft() > 0 );

	for ( i = 0 ; i < count; i++ )
	{
		SoundInfo_t &sound = pGameClient->m_Sounds[ i ];
		sound.WriteDelta( pDeltaSound, msg.m_DataOut );
		pDeltaSound = &pGameClient->m_Sounds[ i ];
	}

	// remove added events from list
	if ( g_sv_sound_discardextraunreliable->GetBool() )
	{
		if ( pGameClient->m_Sounds.Count() != count )
		{
			DevMsg( 2, "Warning! Dropped %i unreliable sounds for client %s.\n" , pGameClient->m_Sounds.Count() - count, pGameClient->GetClientName() );
		}
		pGameClient->m_Sounds.RemoveAll();
	}
	else
	{
		int remove = pGameClient->m_Sounds.Count() - ( count + nMaxSounds );
		if ( remove > 0 )
		{
			DevMsg( 2, "Warning! Dropped %i unreliable sounds for client %s.\n" , remove, pGameClient->GetClientName() );
			count+= remove;
		}

		if ( count > 0 )
		{
			pGameClient->m_Sounds.RemoveMultiple( 0, count );
		}
	}

	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:CGameClient__FillSoundsMessage maxsounds after: %d count: %d, NumSounds: %d", nMaxSounds, pGameClient->m_Sounds.Count(), msg.m_nNumSounds);
	}

	Assert( pGameClient->m_Sounds.Count() <= nMaxSounds ); // keep ev_max temp ent for next update

	return msg.m_nNumSounds;
}

DETOUR_DECL_MEMBER1(CGameClient__FillSoundsMessage, int, SVC_Sounds &, msg)
{
	CGameClient *pGameClient = (CGameClient *)this;

	int nResult = Custom_CGameClient__FillSoundsMessage(pGameClient, msg);

	RETURN_META_VALUE(MRES_SUPERCEDE, nResult);
}

DETOUR_DECL_MEMBER1(CGameClient__WriteGameSounds, void, bf_write, &buf)
{
	CGameClient *pGameClient = (CGameClient *)this;

	if ( pGameClient->m_Sounds.Count() <= 0 )
		return;

	char data[NET_MAX_PAYLOAD];
	SVC_Sounds msg;
	msg.m_DataOut.StartWriting( data, sizeof(data) );
	
	msg.SetReliable( false );
	int nSoundCount = Custom_CGameClient__FillSoundsMessage( pGameClient, msg );
	msg.WriteToBuffer( buf );

	// if ( pGameClient->IsTracing() )
	// {
	// 	pGameClient->TraceNetworkData( buf, "Sounds [count=%d]", nSoundCount );
	// }
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
		g_pSM->LogMessage(myself, "SSF:CBaseServer__WriteTempEntities maxentities: %d", ev_max);
	}

	DETOUR_MEMBER_CALL(CBaseServer__WriteTempEntities)(client, pCurrentSnapshot, pLastSnapshot, buf, ev_max);
}

DETOUR_DECL_MEMBER1(CBaseClient__SendSnapshot, void, CClientFrame *, pFrame)
{
	CBaseClient *pBaseClient = (CBaseClient *)this;

	DETOUR_MEMBER_CALL(CBaseClient__SendSnapshot)(pFrame);
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

	g_Detour_CBaseClient__SendSnapshot = DETOUR_CREATE_MEMBER(CBaseClient__SendSnapshot, "CBaseClient__SendSnapshot");
	if(!g_Detour_CBaseClient__SendSnapshot)
	{
		snprintf(error, maxlen, "Failed to detour CBaseClient__SendSnapshot.\n");
		return false;
	}
	g_Detour_CBaseClient__SendSnapshot->EnableDetour();

	g_Detour_CBaseServer__WriteTempEntities = DETOUR_CREATE_MEMBER(CBaseServer__WriteTempEntities, "CBaseServer__WriteTempEntities");
	if(!g_Detour_CBaseServer__WriteTempEntities)
	{
		snprintf(error, maxlen, "Failed to detour CBaseServer__WriteTempEntities.\n");
		return false;
	}
	g_Detour_CBaseServer__WriteTempEntities->EnableDetour();

	g_Detour_CGameClient__FillSoundsMessage = DETOUR_CREATE_MEMBER(CGameClient__FillSoundsMessage, "CGameClient__FillSoundsMessage");
	if(!g_Detour_CGameClient__FillSoundsMessage)
	{
		snprintf(error, maxlen, "Failed to detour CGameClient__FillSoundsMessage.\n");
		return false;
	}
	g_Detour_CGameClient__FillSoundsMessage->EnableDetour();

	g_Detour_CGameClient__WriteGameSounds = DETOUR_CREATE_MEMBER(CGameClient__WriteGameSounds, "CGameClient__WriteGameSounds");
	if(!g_Detour_CGameClient__WriteGameSounds)
	{
		snprintf(error, maxlen, "Failed to detour CGameClient__WriteGameSounds.\n");
		return false;
	}
	g_Detour_CGameClient__WriteGameSounds->EnableDetour();

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

	if(g_Detour_CBaseServer__WriteTempEntities)
	{
		g_Detour_CBaseServer__WriteTempEntities->Destroy();
		g_Detour_CBaseServer__WriteTempEntities = NULL;
	}

	if(g_Detour_CGameClient__FillSoundsMessage)
	{
		g_Detour_CGameClient__FillSoundsMessage->Destroy();
		g_Detour_CGameClient__FillSoundsMessage = NULL;
	}

	if(g_Detour_CGameClient__WriteGameSounds)
	{
		g_Detour_CGameClient__WriteGameSounds->Destroy();
		g_Detour_CGameClient__WriteGameSounds = NULL;
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