#include <mempool.h>
#include <sourcehook.h>
#include <server_class.h>

class PackedEntity;
class CEventInfo;

#define INVALID_PACKED_ENTITY_HANDLE (0)
typedef intptr_t PackedEntityHandle_t;

//-----------------------------------------------------------------------------
// Purpose: Individual entity data, did the entity exist and what was it's serial number
//-----------------------------------------------------------------------------
class CFrameSnapshotEntry
{
public:
	ServerClass*			m_pClass;
	int						m_nSerialNumber;
	// Keeps track of the fullpack info for this frame for all entities in any pvs:
	PackedEntityHandle_t	m_pPackedData;
};

// HLTV needs some more data per entity 
class CHLTVEntityData
{
public:
	vec_t			origin[3];	// entity position
	unsigned int	m_nNodeCluster;  // if (1<<31) is set it's a node, otherwise a cluster
};

// Replay needs some more data per entity 
class CReplayEntityData
{
public:
	vec_t			origin[3];	// entity position
	unsigned int	m_nNodeCluster;  // if (1<<31) is set it's a node, otherwise a cluster
};

typedef struct
{
	PackedEntity	*pEntity;	// original packed entity
	int				counter;	// increaseing counter to find LRU entries
	int				bits;		// uncompressed data length in bits
	char			data[MAX_PACKEDENTITY_DATA]; // uncompressed data cache
} UnpackedDataCache_t;



//-----------------------------------------------------------------------------
// Purpose: For all entities, stores whether the entity existed and what frame the
//  snapshot is for.  Also tracks whether the snapshot is still referenced.  When no
//  longer referenced, it's freed
//-----------------------------------------------------------------------------
class CFrameSnapshot
{
	DECLARE_FIXEDSIZE_ALLOCATOR( CFrameSnapshot );

public:

							CFrameSnapshot();
							~CFrameSnapshot();

	// Reference-counting.
	void					AddReference();
	void					ReleaseReference();

	CFrameSnapshot*			NextSnapshot() const;						


public:
	CInterlockedInt			m_ListIndex;	// Index info CFrameSnapshotManager::m_FrameSnapshots.

	// Associated frame. 
	int						m_nTickCount; // = sv.tickcount
	
	// State information
	CFrameSnapshotEntry		*m_pEntities;	
	int						m_nNumEntities; // = sv.num_edicts

	// This list holds the entities that are in use and that also aren't entities for inactive clients.
	unsigned short			*m_pValidEntities; 
	int						m_nValidEntities;

	// Additional HLTV info
	CHLTVEntityData			*m_pHLTVEntityData; // is NULL if not in HLTV mode or array of m_pValidEntities entries
	CReplayEntityData		*m_pReplayEntityData; // is NULL if not in replay mode or array of m_pValidEntities entries

	CEventInfo				**m_pTempEntities; // temp entities
	int						m_nTempEntities;

	CUtlVector<int>			m_iExplicitDeleteSlots;

private:

	// Snapshots auto-delete themselves when their refcount goes to zero.
	CInterlockedInt			m_nReferences;
};
