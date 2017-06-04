/*****************************************************************************
                    The Dark Mod GPL Source Code
 
 This file is part of the The Dark Mod Source Code, originally based 
 on the Doom 3 GPL Source Code as published in 2011.
 
 The Dark Mod Source Code is free software: you can redistribute it 
 and/or modify it under the terms of the GNU General Public License as 
 published by the Free Software Foundation, either version 3 of the License, 
 or (at your option) any later version. For details, see LICENSE.TXT.
 
 Project: The Dark Mod (http://www.thedarkmod.com/)
 
 $Revision: 5513 $ (Revision of last commit) 
 $Date: 2012-08-07 17:07:17 +0000 (Tue, 07 Aug 2012) $ (Date of last commit)
 $Author: tels $ (Author of last commit)
 
******************************************************************************/

#include "precompiled_engine.h"
#pragma hdrstop

static bool versioned = RegisterVersionedFile("$Id: AASFileManager.cpp 5513 2012-08-07 17:07:17Z tels $");

#include "AASFile.h"
#include "AASFile_local.h"

/*
===============================================================================

	AAS File Manager

===============================================================================
*/

class idAASFileManagerLocal : public idAASFileManager {
public:
	virtual						~idAASFileManagerLocal( void ) {}

	virtual idAASFile *			LoadAAS( const char *fileName, const unsigned int mapFileCRC );
	virtual void				FreeAAS( idAASFile *file );
};

idAASFileManagerLocal			AASFileManagerLocal;
idAASFileManager *				AASFileManager = &AASFileManagerLocal;


/*
================
idAASFileManagerLocal::LoadAAS
================
*/
idAASFile *idAASFileManagerLocal::LoadAAS( const char *fileName, const unsigned int mapFileCRC ) {
	idAASFileLocal *file = new idAASFileLocal();
	if ( !file->Load( fileName, mapFileCRC ) ) {
		delete file;
		return NULL;
	}
	return file;
}

/*
================
idAASFileManagerLocal::FreeAAS
================
*/
void idAASFileManagerLocal::FreeAAS( idAASFile *file ) {
	delete file;
}
