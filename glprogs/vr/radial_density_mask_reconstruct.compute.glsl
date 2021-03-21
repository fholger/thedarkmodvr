/**
 * Adapted from Ogre: https://github.com/OGRECave/ogre-next under the MIT license
 */
#version 450 core
#extension GL_ARB_shader_group_vote : require

uniform sampler2D u_srcTex;
layout (binding = 0, rgba8) uniform restrict writeonly image2D u_dstTex;

uniform vec2 u_center;
uniform vec2 u_invClusterResolution;
uniform vec2 u_invResolution;
uniform vec3 u_radius;
uniform int u_quality;

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

/** Takes the pattern (low quality):
		ab xx ef xx
		cd xx gh xx
		xx ij xx mn
		xx kl xx op
	And outputs:
		ab ab ef ef
		cd cd gh gh
		ij ij mn mn
		kl kl op op
*/
void reconstructHalfResLow( ivec2 dstUV, uvec2 uFragCoordHalf )
{
	ivec2 offset;
	if( (uFragCoordHalf.x & 0x01u) != (uFragCoordHalf.y & 0x01u) )
		offset.x = (uFragCoordHalf.y & 0x01u) == 0 ? -2 : 2;
	else
		offset.x = 0;
	offset.y = 0;

	ivec2 uv = dstUV + offset;
	vec4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

	imageStore( u_dstTex, dstUV, srcVal );
}

/** For non-rendered pixels, it averages the closest neighbours eg.
		ab
		cd
	 ef xy ij
	 gh zw kl
		mn
		op
	x = avg( f, c )
	y = avg( d, i )
	z = avg( h, m )
	w = avg( n, k )

	For rendered samples, it averages diagonals:
	ef
	gh
		ab
		cd
	outputs:
	h' = avg( a, h )
	a' = avg( a, h )
*/
void reconstructHalfResMedium( ivec2 dstUV, uvec2 uFragCoordHalf )
{
	if( (uFragCoordHalf.x & 0x01u) != (uFragCoordHalf.y & 0x01u) )
	{
		ivec2 offset0;
		ivec2 offset1;
		offset0.x = (dstUV.x & 0x01) == 0 ? -1 : 1;
		offset0.y = 0;

		offset1.x = 0;
		offset1.y = (dstUV.y & 0x01) == 0 ? -1 : 1;

		ivec2 uv0 = ivec2( ivec2( dstUV ) + offset0 );
		vec4 srcVal0 = texelFetch( u_srcTex, uv0.xy, 0 );
		ivec2 uv1 = ivec2( ivec2( dstUV ) + offset1 );
		vec4 srcVal1 = texelFetch( u_srcTex, uv1.xy, 0 );

		imageStore( u_dstTex, dstUV, (srcVal0 + srcVal1) * 0.5f );
	}
	else
	{
		ivec2 uv = ivec2( dstUV );
		vec4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

		ivec2 offset0;
		offset0.x = (dstUV.x & 0x01) == 0 ? -1 : 1;
		offset0.y = (dstUV.y & 0x01) == 0 ? -1 : 1;

		ivec2 uv0 = ivec2( ivec2( dstUV ) + offset0 );
		vec4 srcVal0 = texelFetch( u_srcTex, uv0.xy, 0 );

		imageStore( u_dstTex, dstUV, (srcVal + srcVal0) * 0.5f );
	}
}

/* Uses Valve's Alex Vlachos Advanced VR Rendering Performance technique
   (bilinear approximation) GDC 2016
*/
void reconstructHalfResHigh( ivec2 dstUV, uvec2 uFragCoordHalf )
{
	if( (uFragCoordHalf.x & 0x01u) != (uFragCoordHalf.y & 0x01u) )
	{
		vec2 offset0;
		vec2 offset1;
		offset0.x = (dstUV.x & 0x01) == 0 ? -0.5f : 1.5f;
		offset0.y =	(dstUV.y & 0x01) == 0 ? 0.75f : 0.25f;

		offset1.x = (dstUV.x & 0x01) == 0 ? 0.75f : 0.25f;
		offset1.y = (dstUV.y & 0x01) == 0 ? -0.5f : 1.5f;

		vec2 offset0N = offset0;
		offset0N.x = (dstUV.x & 0x01) == 0 ? 2.5f : -1.5f;
		vec2 offset1N = offset1;
		offset1N.y = (dstUV.y & 0x01) == 0 ? 2.5f : -1.5f;

		vec2 uv0 = ( vec2( dstUV ) + offset0 ) * u_invResolution;
		vec4 srcVal0 = textureLod( u_srcTex, uv0.xy, 0 );
		vec2 uv1 = ( vec2( dstUV ) + offset1 ) * u_invResolution;
		vec4 srcVal1 = textureLod( u_srcTex, uv1.xy, 0 );
		vec2 uv0N = ( vec2( dstUV ) + offset0N ) * u_invResolution;
		vec4 srcVal0N = textureLod( u_srcTex, uv0N.xy, 0 );
		vec2 uv1N = ( vec2( dstUV ) + offset1N ) * u_invResolution;
		vec4 srcVal1N = textureLod( u_srcTex, uv1N.xy, 0 );

		vec4 finalVal = srcVal0 * 0.375f + srcVal1 * 0.375f + srcVal0N * 0.125f + srcVal1N * 0.125f;
		imageStore( u_dstTex, dstUV, finalVal );
	}
	else
	{
		vec2 uv = vec2( dstUV );
		uv.x += (dstUV.x & 0x01) == 0 ? 0.75f : 0.25f;
		uv.y += (dstUV.y & 0x01) == 0 ? 0.75f : 0.25f;
		uv.xy *= u_invResolution;
		vec4 srcVal = textureLod( u_srcTex, uv.xy, 0 );

		ivec2 uv0 = ivec2( uFragCoordHalf << 1u );
		vec4 srcTL = texelFetch( u_srcTex, uv0 + ivec2( -1, -1 ), 0 );
		vec4 srcTR = texelFetch( u_srcTex, uv0 + ivec2(  2, -1 ), 0 );
		vec4 srcBL = texelFetch( u_srcTex, uv0 + ivec2( -1,  2 ), 0 );
		vec4 srcBR = texelFetch( u_srcTex, uv0 + ivec2(  2,  2 ), 0 );

		float weights[4] = float[] ( 0.28125f, 0.09375f, 0.09375f, 0.03125f );

		int idx = (dstUV.x & 0x01) + ((dstUV.y & 0x01) << 1u);

		vec4 finalVal =	srcVal * 0.5f +
							srcTL * weights[(idx + 0)] +
							srcTR * weights[(idx + 1) & 0x03] +
							srcBL * weights[(idx + 2) & 0x03] +
							srcBR * weights[(idx + 3) & 0x03];

		imageStore( u_dstTex, dstUV, finalVal );
	}
}

/** Takes the pattern:
		a b x x
		c d x x
		x x x x
		x x x x
	And outputs:
		a b a b
		c d c d
		a b a b
		c d c d
*/
void reconstructQuarterRes( ivec2 dstUV, uvec2 uFragCoordHalf )
{
	ivec2 offset;
	offset.x = (uFragCoordHalf.x & 0x01u) == 0 ? 0 : -2;
	offset.y = (uFragCoordHalf.y & 0x01u) == 0 ? 0 : -2;

	ivec2 uv = ivec2( ivec2( dstUV ) + offset );
	vec4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

	imageStore( u_dstTex, dstUV, srcVal );
}

/** Same as reconstructQuarterRes, but a lot more samples to repeat:
		a b x x x x x x
		c d x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
		x x x x x x x x
	And outputs:
		a b a b a b a b
		c d c d c d c d
		a b a b a b a b
		c d c d c d c d
		a b a b a b a b
		c d c d c d c d
		a b a b a b a b
		c d c d c d c d
*/
void reconstructSixteenthRes( ivec2 dstUV, uvec2 uFragCoordHalf )
{
	ivec2 block = ivec2( uFragCoordHalf ) & 0x03;

	ivec2 offset;
	offset.x = block.x * -2;
	offset.y = block.y * -2;

	ivec2 uv = ivec2( ivec2( dstUV ) + offset );
	vec4 srcVal = texelFetch( u_srcTex, uv.xy, 0 );

	imageStore( u_dstTex, dstUV, srcVal );
}

void main() {
	uvec2 currentUV = uvec2(gl_GlobalInvocationID.xy);
	uvec2 uFragCoordHalf = uvec2(currentUV >> 1u);

	//We must work in blocks so the reconstruction filter can work properly
	vec2 toCenter = (currentUV >> 3u) * u_invClusterResolution - u_center;
	float distToCenter = 2 * length(toCenter);

	//We know for a fact distToCenter is in blocks of 8x8
	if( anyInvocationARB( distToCenter >= u_radius.x ) )
	{
		if( anyInvocationARB( distToCenter < u_radius.y ) )
		{
			if( anyInvocationARB( distToCenter + u_invClusterResolution.x < u_radius.y ) )
			{
				if (u_quality == 0) 
					reconstructHalfResLow( ivec2(currentUV), uFragCoordHalf );
				else if (u_quality == 1)
					reconstructHalfResMedium( ivec2(currentUV), uFragCoordHalf );
				else
					reconstructHalfResHigh( ivec2(currentUV), uFragCoordHalf );
			}
			else
			{
				//Right next to the border with lower res rendering.
				//We can't use anything else than low quality filter
				reconstructHalfResLow( ivec2( currentUV ), uFragCoordHalf );
			}
		}
		else if( anyInvocationARB( distToCenter < u_radius.z ) )
		{
			reconstructQuarterRes( ivec2( currentUV ), uFragCoordHalf );
		}
		else
		{
			reconstructSixteenthRes( ivec2( currentUV ), uFragCoordHalf );
		}
	}
	else
	{
		imageStore( u_dstTex, ivec2(currentUV), texelFetch( u_srcTex, ivec2( currentUV ), 0 ) );
	}
}
