#include "extension.h"

// Func_CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__DeleteFrameSnapshot;
// Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot;
// CThreadFastMutex		m_FrameSnapshotsWriteMutex;

DEFINE_FIXEDSIZE_ALLOCATOR( CFrameSnapshot, 64, 64 );

// ------------------------------------------------------------------------------------------------ //
// CFrameSnapshotManager
// ------------------------------------------------------------------------------------------------ //
void DeleteFrameSnapshot( CFrameSnapshot* pSnapshot )
{
	// Decrement reference counts of all packed entities
	for (int i = 0; i < pSnapshot->m_nNumEntities; ++i)
	{
		if ( pSnapshot->m_pEntities[i].m_pPackedData != INVALID_PACKED_ENTITY_HANDLE )
		{
			g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__RemoveEntityReference( pSnapshot->m_pEntities[i].m_pPackedData );
//			RemoveEntityReference( pSnapshot->m_pEntities[i].m_pPackedData );
		}
	}

	g_Func_CFrameSnapshot__m_FrameSnapshots__Remove( pSnapshot->m_ListIndex );
//	m_FrameSnapshots.Remove( pSnapshot->m_ListIndex );
	delete pSnapshot;
}

// ------------------------------------------------------------------------------------------------ //
// CFrameSnapshot
// ------------------------------------------------------------------------------------------------ //

#if defined( _DEBUG )
	int g_nAllocatedSnapshots = 0;
#endif

CFrameSnapshot::CFrameSnapshot()
{
	m_nTempEntities = 0;
	m_pTempEntities = NULL;
	m_pValidEntities = NULL;
	m_nReferences = 0;
#if defined( _DEBUG )
	++g_nAllocatedSnapshots;
	Assert( g_nAllocatedSnapshots < 80000 ); // this probably would indicate a memory leak.
#endif
}

CFrameSnapshot::~CFrameSnapshot()
{
	delete [] m_pValidEntities;
	delete [] m_pEntities;

	if ( m_pTempEntities )
	{
		Assert( m_nTempEntities>0 );
		for (int i = 0; i < m_nTempEntities; i++ )
		{
			delete m_pTempEntities[i];
		}

		delete [] m_pTempEntities;
	}

	if ( m_pHLTVEntityData )
	{
		delete [] m_pHLTVEntityData;
	}

	if ( m_pReplayEntityData )
	{
		delete [] m_pReplayEntityData;
	}
	Assert ( m_nReferences == 0 );

#if defined( _DEBUG )
	--g_nAllocatedSnapshots;
	Assert( g_nAllocatedSnapshots >= 0 );
#endif
}

void CFrameSnapshot::AddReference()
{
	Assert( m_nReferences < 0xFFFF );
	++m_nReferences;
}

// Keep list building thread-safe
// This lock was moved to to fix bug https://bugbait.valvesoftware.com/show_bug.cgi?id=53403
// Crash in CFrameSnapshotManager::GetPackedEntity where a CBaseClient's m_pBaseline snapshot could be removed the CReferencedSnapshotList destructor 
// for another client that is in WriteTempEntities

void CFrameSnapshot::ReleaseReference()
{
	//AUTO_LOCK_FM(m_FrameSnapshotsWriteMutex);

	Assert( m_nReferences > 0 );

	--m_nReferences;
	if ( m_nReferences == 0 )
	{
		DeleteFrameSnapshot( this );
//		g_FrameSnapshotManager.DeleteFrameSnapshot( this );
	}
}

CFrameSnapshot* CFrameSnapshot::NextSnapshot() const
{
	return g_Func_CFrameSnapshotManager__g_FrameSnapshotManager__NextSnapshot( this );
//	return g_FrameSnapshotManager.NextSnapshot( this );
}
