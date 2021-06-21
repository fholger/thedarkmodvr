/*****************************************************************************
The Dark Mod GPL Source Code

This file is part of the The Dark Mod Source Code, originally based
on the Doom 3 GPL Source Code as published in 2011.

The Dark Mod Source Code is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the License,
or (at your option) any later version. For details, see LICENSE.TXT.

Project: The Dark Mod (http://www.thedarkmod.com/)

******************************************************************************/

#include "precompiled.h"
#pragma hdrstop



#include "GEApp.h"
#include "GEKeyValueModifier.h"

rvGEKeyValueModifier::rvGEKeyValueModifier ( const char* name, idWindow* window, const char* key, const char* value ) : 	
	rvGEModifier ( name, window ),
	mKey ( key ),
	mValue ( value )
{
	mUndoValue = mWrapper->GetStateDict().GetString ( mKey );
}

bool rvGEKeyValueModifier::Apply ( void )
{
	if ( mValue.Length ( ) )
	{
		mWrapper->SetStateKey ( mKey, mValue );
	}
	else
	{
		mWrapper->DeleteStateKey ( mKey );
	}

	return true;
}

bool rvGEKeyValueModifier::Undo ( void )
{
	mWrapper->SetStateKey ( mKey, mValue );

	return true;
}

bool rvGEKeyValueModifier::Merge ( rvGEModifier* mergebase )
{
	rvGEKeyValueModifier* merge = (rvGEKeyValueModifier*) mergebase;

	mValue = merge->mValue;	
	
	return true;
} 
