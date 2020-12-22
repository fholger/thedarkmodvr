/**
 * Adapted from Ogre: https://github.com/OGRECave/ogre-next under the MIT license
 */
#version 330 core

out vec4 FragColor;

uniform vec3 u_radius;
uniform vec2 u_invClusterResolution;

void main() {
    vec2 clusterUV = trunc(gl_FragCoord.xy * 0.125f) * u_invClusterResolution;
    float distToCenter = 2 * length(clusterUV - vec2(0.5f));
    
    uvec2 fragCoordHalf = uvec2(gl_FragCoord.xy * 0.5f);
    // everything in the inner radius is preserved
    if (distToCenter < u_radius.x) {
        discard;
    }
    // mask every second 2x2 pixel block in the cluster
    if ((fragCoordHalf.x & 1u) == (fragCoordHalf.y & 1u) && distToCenter < u_radius.y) {
        discard;
    }
    // only render two 2x2 pixel blocks in the cluster
    if (!((fragCoordHalf.x & 1u) != 0u || (fragCoordHalf.y & 1u) != 0u) && distToCenter < u_radius.z) {
        discard;
    }
    // only render one 2x2 pixel block in the cluster
    if (!((fragCoordHalf.x & 3u) != 0u || (fragCoordHalf.y & 3u) != 0u)) {
        discard;
    }

    FragColor = vec4(0,0,0,1);
}
