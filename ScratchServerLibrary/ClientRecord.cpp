#include "pch.h"
#include "ClientRecord.h"

bool ClientRecord::TryInsertNewNetworkObject(int objectID, Snapshot SnapshotToAdd)
{
	if (networkedObjects.contains(objectID))
	{
		return false; //we already have 
	}

	networkedObjects[objectID] = SnapshotToAdd;
	return true;
}

bool ClientRecord::TryUpdatingNetworkedObject(int objectID, Snapshot SnapshotToUpdate)
{
	if (!networkedObjects.contains(objectID))
	{
		return false; //we cant find
	}



	networkedObjects[objectID] = SnapshotToUpdate;
	return true;
}

