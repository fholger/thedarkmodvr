/*****************************************************************************
                    The Dark Mod GPL Source Code
 
 This file is part of the The Dark Mod Source Code, originally based 
 on the Doom 3 GPL Source Code as published in 2011.
 
 The Dark Mod Source Code is free software: you can redistribute it 
 and/or modify it under the terms of the GNU General Public License as 
 published by the Free Software Foundation, either version 3 of the License, 
 or (at your option) any later version. For details, see LICENSE.TXT.
 
 Project: The Dark Mod (http://www.thedarkmod.com/)
 
 $Revision: 5121 $ (Revision of last commit) 
 $Date: 2011-12-11 19:12:26 +0000 (Sun, 11 Dec 2011) $ (Date of last commit)
 $Author: greebo $ (Author of last commit)
 
******************************************************************************/

// Copyright (C) 2004 Id Software, Inc.
//
// DebugGraph.h

class idDebugGraph {
public:
					idDebugGraph();
	void			SetNumSamples( int num );
	void			AddValue( float value );
	void			Draw( const idVec4 &color, float scale ) const;

private:
	idList<float>	samples;
	int				index;
};
