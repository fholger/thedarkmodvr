/*****************************************************************************
                    The Dark Mod GPL Source Code
 
 This file is part of the The Dark Mod Source Code, originally based 
 on the Doom 3 GPL Source Code as published in 2011.
 
 The Dark Mod Source Code is free software: you can redistribute it 
 and/or modify it under the terms of the GNU General Public License as 
 published by the Free Software Foundation, either version 3 of the License, 
 or (at your option) any later version. For details, see LICENSE.TXT.
 
 Project: The Dark Mod (http://www.thedarkmod.com/)
 
 $Revision: 5935 $ (Revision of last commit) 
 $Date: 2014-02-15 19:19:00 +0000 (Sat, 15 Feb 2014) $ (Date of last commit)
 $Author: grayman $ (Author of last commit)
 
******************************************************************************/

#include "precompiled_game.h"
#pragma hdrstop

static bool versioned = RegisterVersionedFile("$Id: PathTask.cpp 5935 2014-02-15 19:19:00Z grayman $");

#include "../Memory.h"
#include "PathTask.h"
#include "../Library.h"

namespace ai
{

PathTask::PathTask()
{}

PathTask::PathTask(idPathCorner* path)
{
	_path = path;
}

void PathTask::Init(idAI* owner, Subsystem& subsystem)
{
	// Just init the base class
	Task::Init(owner, subsystem);

	if (_path.GetEntity() == NULL)
	{
		gameLocal.Error("Path Entity not set before Init()");
	}

	idPathCorner* path = _path.GetEntity();

	_accuracy = path->spawnArgs.GetFloat("move_to_position_tolerance", "-1");
}


void PathTask::SetTargetEntity(idPathCorner* path) 
{
	assert(path);
	_path = path;
}

// Save/Restore methods
void PathTask::Save(idSaveGame* savefile) const
{
	Task::Save(savefile);

	_path.Save(savefile);
	savefile->WriteFloat(_accuracy);
	savefile->WriteBool(_activateTargets); // grayman #3670
}

void PathTask::Restore(idRestoreGame* savefile)
{
	Task::Restore(savefile);

	_path.Restore(savefile);
	savefile->ReadFloat(_accuracy);
	savefile->ReadBool(_activateTargets); // grayman #3670
}


} // namespace ai
