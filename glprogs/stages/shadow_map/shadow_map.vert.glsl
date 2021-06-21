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
#version 330 core
#pragma tdm_define "MAX_SHADER_PARAMS"

struct PerDrawCallParams {
	mat4 modelMatrix;
};

layout (std140) uniform PerDrawCallParamsBlock {
	PerDrawCallParams params[MAX_SHADER_PARAMS];
};

uniform vec4 u_lightOrigin;
uniform float u_lightRadius;

in vec4 attr_Position;
in vec4 attr_TexCoord;
in int attr_DrawId;

out vec2 texCoord;

const mat3 cubicTransformations[6] = mat3[6] (
    mat3(
        0, 0, -1,
        0, -1, 0,
        -1, 0, 0
    ),
    mat3(
        0, 0, 1,
        0, -1, 0,
        1, 0, 0
    ),
    mat3(
        1, 0, 0,
        0, 0, -1,
        0, 1, 0
    ),
    mat3(
        1, 0, 0,
        0, 0, 1,
        0, -1, 0
    ),
    mat3(
        1, 0, 0,
        0, -1, 0,
        0, 0, -1
    ),
    mat3(
        -1, 0, 0,
        0, -1, 0,
        0, 0, 1
    )
);

const float clipEps = 0e-2;
const vec4 ClipPlanes[4] = vec4[4] (
    vec4(1, 0, -1, clipEps),
    vec4(-1, 0, -1, clipEps),
    vec4(0, 1, -1, clipEps),
    vec4(0, -1, -1, clipEps)
);

void main() {
    texCoord = (attr_TexCoord).st;
    vec4 lightSpacePos = params[attr_DrawId].modelMatrix * attr_Position - u_lightOrigin;
    vec4 fragPos = vec4(cubicTransformations[gl_InstanceID] * lightSpacePos.xyz, 1);
    gl_Position.x = fragPos.x / 6 + fragPos.z * 5/6 - fragPos.z / 3 * gl_InstanceID;
    gl_Position.y = fragPos.y;
    gl_Position.z = -fragPos.z - 2*u_lightRadius;
    gl_Position.w = -fragPos.z;
    gl_ClipDistance[0] = dot(fragPos, ClipPlanes[0]);
    gl_ClipDistance[1] = dot(fragPos, ClipPlanes[1]);
    gl_ClipDistance[2] = dot(fragPos, ClipPlanes[2]);
    gl_ClipDistance[3] = dot(fragPos, ClipPlanes[3]);
}