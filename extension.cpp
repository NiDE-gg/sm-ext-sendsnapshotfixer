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
#include <smartptr.h>
#include <platform.h>
#include <inetchannel.h>
#include <vprof.h>

extern ConVar sv_netspike_on_reliable_snapshot_overflow;

extern  float		host_frametime_unbounded;
extern  float		host_frametime_stddeviation;

class NET_Tick
{
public:
	NET_Tick( int tick, float hostFrametime, float hostFrametime_stddeviation ) 
	{
	}
public:
	bool WriteToBuffer( bf_write &buffer );
};

class CLocalNetworkBackdoor;

// The client will set this if it decides to use the fast path.
extern CLocalNetworkBackdoor *g_pLocalNetworkBackdoor;

#define TRACE_PACKET( text )
#define tmZoneFiltered(...)
#define TMZF_NONE	0x0000

typedef char *HTELEMETRY;

struct TelemetryData
{
	HTELEMETRY tmContext[32];
};

PLATFORM_INTERFACE TelemetryData g_Telemetry;

#define TELEMETRY_LEVEL0	g_Telemetry.tmContext[0]	// high level tmZone()
#define TELEMETRY_LEVEL1	g_Telemetry.tmContext[1]	// lower level tmZone(), tmZoneFiltered()
#define TELEMETRY_LEVEL2	g_Telemetry.tmContext[2]	// VPROF_0
#define TELEMETRY_LEVEL3	g_Telemetry.tmContext[3]	// VPROF_1
#define TELEMETRY_LEVEL4	g_Telemetry.tmContext[4]	// VPROF_2
#define TELEMETRY_LEVEL5	g_Telemetry.tmContext[5]	// VPROF_3
#define TELEMETRY_LEVEL6	g_Telemetry.tmContext[6]	// VPROF_4

class CFrameSnapshot
{
public:
	void					AddReference();
	void					ReleaseReference();
};

class CClientFrame
{
public:
	CFrameSnapshot*		GetSnapshot();
public:
	int					tick_count;	// server tick of this snapshot
private:

	// Index of snapshot entry that stores the entities that were active and the serial numbers
	// for the frame number this packed entity corresponds to
	// m_pSnapshot MUST be private to force using SetSnapshot(), see reference counters
	CFrameSnapshot		*m_pSnapshot;
};

class CBaseServer;

class CBaseClient
{
public:
	bool			IsTracing();
	void			TraceNetworkData( bf_write &msg, PRINTF_FORMAT_STRING char const *fmt, ... );
	void			TraceNetworkMsg( int nBits, PRINTF_FORMAT_STRING char const *fmt, ... );

	virtual CClientFrame *GetDeltaFrame( int nTick );
	virtual	int		GetMaxAckTickCount() const;
	virtual	void	Disconnect( PRINTF_FORMAT_STRING const char *reason, ... );
	virtual void	WriteGameSounds(bf_write &buf);

public:

	void			OnRequestFullUpdate();

public:

	// Array index in svs.clients:
	int				m_nClientSlot;	
	// entity index of this client (different from clientSlot+1 in HLTV and Replay mode):
	int				m_nEntityIndex;	
	
	char			m_Name[MAX_PLAYER_NAME_LENGTH];			// for printing to other people

	CBaseServer		*m_Server;			// pointer to server object
	bool			m_bIsHLTV;			// if this a HLTV proxy ?

	bool			m_bIsReplay;		// if this is a Replay proxy ?

	//===== NETWORK ============
	INetChannel		*m_NetChannel;		// The client's net connection.
	int				m_nSignonState;		// connection state
	int				m_nDeltaTick;		// -1 = no compression.  This is where the server is creating the
										// compressed info from.
	int				m_nStringTableAckTick; // Highest tick acked for string tables (usually m_nDeltaTick, except when it's -1)
	int				m_nSignonTick;		// tick the client got his signon data
	CSmartPtr<CFrameSnapshot,CRefCountAccessorLongName> m_pLastSnapshot;	// last send snapshot

		
	// This is used when we send out a nodelta packet to put the client in a state where we wait 
	// until we get an ack from them on this packet.
	// This is for 3 reasons:
	// 1. A client requesting a nodelta packet means they're screwed so no point in deluging them with data.
	//    Better to send the uncompressed data at a slow rate until we hear back from them (if at all).
	// 2. Since the nodelta packet deletes all client entities, we can't ever delta from a packet previous to it.
	// 3. It can eat up a lot of CPU on the server to keep building nodelta packets while waiting for
	//    a client to get back on its feet.
	int				m_nForceWaitForTick;
	
	bool			m_bFakePlayer;		// JAC: This client is a fake player controlled by the game DLL

	enum
	{
		SNAPSHOT_SCRATCH_BUFFER_SIZE = 160000,
	};

	unsigned int		m_SnapshotScratchBuffer[ SNAPSHOT_SCRATCH_BUFFER_SIZE / 4 ];

public:
	void				StartTrace( bf_write &msg );
	void				EndTrace( bf_write &msg );

	int					m_iTracing; // 0 = not active, 1 = active for this frame, 2 = forced active
};

class CNetworkStringTableContainer
{
public:
	void		WriteUpdateMessage( CBaseClient *client, int tick_ack, bf_write &buf );
};

class CBaseServer
{
public:
	virtual void	WriteDeltaEntities( CBaseClient *client, CClientFrame *to, CClientFrame *from,	bf_write &pBuf );
	virtual void	WriteTempEntities( CBaseClient *client, CFrameSnapshot *to, CFrameSnapshot *from, bf_write &pBuf, int nMaxEnts );	
	virtual bool	IsMultiplayer( void );
public:
	CNetworkStringTableContainer *m_StringTables;
};

SSF g_SSF;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_SSF);

IGameConfig *g_pGameConf = NULL;
CGlobalVars *gpGlobals = NULL;

CDetour *g_Detour_CBaseClient__SendSnapshot = NULL;

ConVar *g_SvSSFLog = CreateConVar("sv_ssf_log", "0", FCVAR_NOTIFY, "Log ssf debug print statements.");

DETOUR_DECL_MEMBER1(CBaseClient__SendSnapshot, void, CClientFrame *, pFrame)
{
	CBaseClient *pBaseClient = (CBaseClient *)this;

	// never send the same snapshot twice
	if ( pBaseClient->m_pLastSnapshot == pFrame->GetSnapshot() )
	{
		pBaseClient->m_NetChannel->Transmit();
		return;
	}

	// if we send a full snapshot (no delta-compression) before, wait until client
	// received and acknowledge that update. don't spam client with full updates
	if ( pBaseClient->m_nForceWaitForTick > 0 )
	{
		// just continue transmitting reliable data
		pBaseClient->m_NetChannel->Transmit();	
		return;
	}

	VPROF_BUDGET( "SendSnapshot", VPROF_BUDGETGROUP_OTHER_NETWORKING );
	tmZoneFiltered( TELEMETRY_LEVEL0, 50, TMZF_NONE, "%s", __FUNCTION__ );

	bool bFailedOnce = false;
write_again:
	bf_write msg( "CBaseClient::SendSnapshot", pBaseClient->m_SnapshotScratchBuffer, sizeof( pBaseClient->m_SnapshotScratchBuffer ) );

	TRACE_PACKET( ( "SendSnapshot(%d)\n", pFrame->tick_count ) );

	// now create client snapshot packet
	CClientFrame *deltaFrame = pBaseClient->GetDeltaFrame( pBaseClient->m_nDeltaTick ); // NULL if delta_tick is not found
	if ( !deltaFrame )
	{
		// We need to send a full update and reset the instanced baselines
		pBaseClient->OnRequestFullUpdate();
	}

	// send tick time
	NET_Tick tickmsg( pFrame->tick_count, host_frametime_unbounded, host_frametime_stddeviation );

	pBaseClient->StartTrace( msg );

	tickmsg.WriteToBuffer( msg );

	if ( pBaseClient->IsTracing() )
	{
		pBaseClient->TraceNetworkData( msg, "NET_Tick" );
	}

#ifndef SHARED_NET_STRING_TABLES
	// in LocalNetworkBackdoor mode we updated the stringtables already in SV_ComputeClientPacks()
	if ( !g_pLocalNetworkBackdoor )
	{
		// Update shared client/server string tables. Must be done before sending entities
		pBaseClient->m_Server->m_StringTables->WriteUpdateMessage( pBaseClient, pBaseClient->GetMaxAckTickCount(), msg );
	}
#endif

	int nDeltaStartBit = 0;
	if ( pBaseClient->IsTracing() )
	{
		nDeltaStartBit = msg.GetNumBitsWritten();
	}

	// send entity update, delta compressed if deltaFrame != NULL
	pBaseClient->m_Server->WriteDeltaEntities( pBaseClient, pFrame, deltaFrame, msg );

	if ( pBaseClient->IsTracing() )
	{
		int nBits = msg.GetNumBitsWritten() - nDeltaStartBit;
		pBaseClient->TraceNetworkMsg( nBits, "Total Delta" );
	}

	// send all unreliable temp entities between last and current frame
	// send max 64 events in multi player, 255 in SP
	int nMaxTempEnts = pBaseClient->m_Server->IsMultiplayer() ? 64 : 255;
	pBaseClient->m_Server->WriteTempEntities( pBaseClient, pFrame->GetSnapshot(), pBaseClient->m_pLastSnapshot.GetObject(), msg, nMaxTempEnts );

	if ( pBaseClient->IsTracing() )
	{
		pBaseClient->TraceNetworkData( msg, "Temp Entities" );
	}

	pBaseClient->WriteGameSounds( msg );
	
	// write message to packet and check for overflow
	if ( msg.IsOverflowed() )
	{
		bool bWasTracing = pBaseClient->IsTracing();
		if ( bWasTracing )
		{
			pBaseClient->TraceNetworkMsg( 0, "Finished [delta %s]", deltaFrame ? "yes" : "no" );
			pBaseClient->EndTrace( msg );
		}

		if ( !deltaFrame )
		{

			if ( !bWasTracing )
			{

				// Check for debugging by dumping a snapshot
				if ( sv_netspike_on_reliable_snapshot_overflow.GetBool() )
				{
					if ( !bFailedOnce ) // shouldn't be necessary, but just in case
					{
						Warning(" RELIABLE SNAPSHOT OVERFLOW!  Triggering trace to see what is so large\n" );
						bFailedOnce = true;
						pBaseClient->m_iTracing = 2;
						goto write_again;
					}
					pBaseClient->m_iTracing = 0;
				}
			}

			// if this is a reliable snapshot, drop the client
			pBaseClient->Disconnect( "ERROR! Reliable snapshot overflow." );
			return;
		}
		else
		{
			// unreliable snapshots may be dropped
			ConMsg ("WARNING: msg overflowed for %s\n", pBaseClient->m_Name);
			msg.Reset();
		}
	}

	// remember this snapshot
	pBaseClient->m_pLastSnapshot = pFrame->GetSnapshot();

	// Don't send the datagram to fakeplayers unless sv_stressbots is on (which will make m_NetChannel non-null).
	if ( pBaseClient->m_bFakePlayer && !pBaseClient->m_NetChannel )
	{
		pBaseClient->m_nDeltaTick = pFrame->tick_count;
		pBaseClient->m_nStringTableAckTick = pBaseClient->m_nDeltaTick;
		return;
	}

	bool bSendOK;

	// is this is a full entity update (no delta) ?
	if ( !deltaFrame )
	{
		VPROF_BUDGET( "SendSnapshot Transmit Full", VPROF_BUDGETGROUP_OTHER_NETWORKING );

		// transmit snapshot as reliable data chunk
		bSendOK = pBaseClient->m_NetChannel->SendData( msg );
		bSendOK = bSendOK && pBaseClient->m_NetChannel->Transmit();

		// remember this tickcount we send the reliable snapshot
		// so we can continue sending other updates if this has been acknowledged
		pBaseClient->m_nForceWaitForTick = pFrame->tick_count;
	}
	else
	{
		VPROF_BUDGET( "SendSnapshot Transmit Delta", VPROF_BUDGETGROUP_OTHER_NETWORKING );

		// just send it as unreliable snapshot
		bSendOK = pBaseClient->m_NetChannel->SendDatagram( &msg ) > 0;
	}
		
	if ( bSendOK )
	{
		if ( pBaseClient->IsTracing() )
		{
			pBaseClient->TraceNetworkMsg( 0, "Finished [delta %s]", deltaFrame ? "yes" : "no" );
			pBaseClient->EndTrace( msg );
		}
	}
	else
	{
		pBaseClient->Disconnect( "ERROR! Couldn't send snapshot." );
	}

	if (g_SvSSFLog->GetBool())
	{
		g_pSM->LogMessage(myself, "SSF:Inside SendSnapshot");
	}
//	return DETOUR_MEMBER_CALL(CBaseClient__SendSnapshot)(pFrame);
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
}

bool SSF::RegisterConCommandBase(ConCommandBase *pVar)
{
	/* Always call META_REGCVAR instead of going through the engine. */
    return META_REGCVAR(pVar);
}