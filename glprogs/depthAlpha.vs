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
#version 140

#pragma tdm_include "tdm_transform.glsl"

INATTR_POSITION  //in vec4 attr_Position;

uniform vec4 u_clipPlane;
uniform mat4 u_matViewRev;

in vec2 attr_TexCoord;

out float clipPlaneDist; 
out vec4 var_TexCoord0;

void main() {
	var_TexCoord0 = u_textureMatrix * vec4(attr_TexCoord, 0, 1);
	clipPlaneDist = dot(u_matViewRev * u_modelViewMatrix * attr_Position, u_clipPlane);
	gl_Position = tdm_transform(attr_Position);
}