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
#ifndef GEMOVEMODIFIER_H_
#define GEMOVEMODIFIER_H_

class rvGEMoveModifier : public rvGEModifier
{
public:

	rvGEMoveModifier ( const char* name, idWindow* window, float x, float y );

	virtual bool		CanMerge	( rvGEModifier* merge );
	virtual bool		Merge		( rvGEModifier* merge );
	
	virtual bool		Apply		( void );
	virtual bool		Undo		( void );
	
	virtual bool		IsValid		( void );
	
protected:

	idRectangle		mNewRect;
	idRectangle		mOldRect;
};
 
ID_INLINE bool rvGEMoveModifier::CanMerge ( rvGEModifier* merge )
{
	return true;
}

#endif // GEMOVEMODIFIER_H_