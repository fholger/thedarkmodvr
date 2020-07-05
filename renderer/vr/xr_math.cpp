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
#include "xr_math.h"

/*
 * Doom 3 / TDM coordinate system:
 *    x is forward
 *    y is left
 *    z is up
 * distance in world units (1 unit = 2.309 cm, or 1.1 units = 1 inch)
 *
 * OpenXR coordinate system:
 *    x is right
 *    y is up
 *   -z is forward
 * distance in metres
 */

const float GameUnitsToMetres = 0.02309f;
const float MetresToGameUnits = 1.0f / GameUnitsToMetres;

idVec3 Vec3FromXr( XrVector3f vec3 ) {
	return idVec3( -vec3.z, -vec3.x, vec3.y ) * MetresToGameUnits;
}

idQuat QuatFromXr( XrQuaternionf quat ) {
	return idQuat( quat.z, quat.x, -quat.y, quat.w );
}
