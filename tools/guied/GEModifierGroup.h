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
#ifndef GEMODIFIERGROUP_H_
#define GEMODIFIERGROUP_H_

#ifndef GEMODIFIER_H_
#include "GEModifier.h"
#endif

class rvGEModifierGroup : public rvGEModifier
{
public:

	rvGEModifierGroup ( );
	~rvGEModifierGroup ( );

	virtual bool		Apply		( void );
	virtual bool		Undo		( void );

	virtual bool		CanMerge	( rvGEModifier* merge );
		
	virtual bool		Merge		( rvGEModifier* merge );
	
	virtual bool		IsValid		( void );

	bool				Append		( rvGEModifier* mod );	
	int					GetCount	( void );
	
	
protected:

	idList<rvGEModifier*>	mModifiers;
		
};

ID_INLINE int rvGEModifierGroup::GetCount( void )
{
	return mModifiers.Num ( );
}

#endif