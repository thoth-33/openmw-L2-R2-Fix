#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#include "lib/core/fragment.h.glsl"

// This is a heavily modified version of OpenMW 0.49's water shader
// Which is inspired by Blender GLSL Water by martinsh ( https://devlog-martinsh.blogspot.de/2012/07/waterundewater-shader-wip.html )

// tweakables -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

const vec2 BIG_WAVES = vec2(0.3, 0.3);               // strength of big waves
const vec2 RAIN_WAVES = vec2(0.5, 0.5);              // strength of rain waves
const vec4 SMALL_WAVES = vec4(0.2, 0.2, 0.2, 0.2);   // strength of small waves

const float WAVE_CHOPPYNESS = 0.05;                  // wave choppyness
const float WAVE_SCALE = 50.0;                       // overall wave scale

const float BUMP = 4.0;                              // overall bumpiness
const float REFL_BUMP = 0.5;                         // reflection distortion amount
const float REFR_BUMP = 0.03;                        // refraction distortion amount

const vec2 DISTORT_SHARP = vec2(0.2, 0.2);           // distortion multiplier at sharp viewing angles

const float SUN_SPEC_FADING_THRESHOLD = 1.0;         // visibility at which sun specularity starts to fade
const float SPEC_HARDNESS = 128.0;                   // specular highlights hardness

const float BUMP_SUPPRESS_DEPTH = 300.0;             // at what water depth bumpmap will be suppressed for reflections and refractions (prevents artifacts at shores)

const vec3 ENV_REDUCE_COLOR = vec3(255, 255, 255) / 255;   // value from Morrowind.ini, tint color for reflection
const vec3 LERP_CLOSE_COLOR = vec3(37, 46, 48) / 255;      // value from Morrowind.ini, fade color for reflection below the camera
const vec3 BUMP_FADE_COLOR = vec3(230, 239, 255) / 255;    // value from Morrowind.ini, water normal multiplier before fresnel calculation
const float ALPHA_REDUCE = 0.35;                           // value from Morrowind.ini, overall transparency reduction value

const float LERP_CLOSE_INTENSITY = 1.0;                    // intensity of reflection fade
const float LERP_CLOSE_AMBIENT = 1.0;                      // influence of ambient light over reflection fade color
const vec3 BASE_AMBIENT = 255 / vec3(137, 140, 160);       // base ambient value under which fade color won't be influenced

#if @wobblyShores
const float WOBBLY_SHORE_FADE_DISTANCE = 6200.0;   // fade out wobbly shores to mask precision errors, the effect is almost impossible to see at a distance
#endif

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -

vec2 normalCoords(vec2 uv, float scale, float time, vec2 speed, vec2 previousNormal)
{
    return uv * (WAVE_SCALE * scale) - previousNormal * WAVE_CHOPPYNESS + speed * time;
}

uniform sampler2D rippleMap;

varying vec3 worldPos;

varying vec2 rippleMapUV;

varying vec4 position;
varying float linearDepth;

uniform sampler2D normalMap;

uniform float osg_SimulationTime;

uniform float near;
uniform float far;

uniform float rainIntensity;
uniform bool enableRainRipples;

uniform vec2 screenRes;

#define PER_PIXEL_LIGHTING 0

#include "shadows_fragment.glsl"
#include "lib/light/lighting.glsl"
#include "fog.glsl"
#include "lib/water/rain_ripples_classic.glsl"
#include "lib/view/depth.glsl"

void main(void)
{
    vec2 UV = worldPos.xy / 8192.0;

    vec2 screenCoords = gl_FragCoord.xy / screenRes;

    vec3 sunWorldDir = normalize((gl_ModelViewMatrixInverse * vec4(lcalcPosition(0).xyz, 0.0)).xyz);
    vec3 cameraPos = (gl_ModelViewMatrixInverse * vec4(0.0,0.0,0.0,1.0)).xyz;
    vec3 viewDir = normalize(position.xyz - cameraPos.xyz);

    float radialDepth = distance(position.xyz, cameraPos);

    vec2 distortMult = mix(DISTORT_SHARP, vec2(1.0), vec2(abs(viewDir.z)));

    // shenanigans to compensate for sampling weirdness: pre-multiply the normal map and divide the weirdness away
    vec2 sampleMult = vec2(0.125);

    #define waterTimer osg_SimulationTime

    vec2 bigWaves = BIG_WAVES;
    vec2 rainWaves = RAIN_WAVES * rainIntensity;
    vec4 smallWaves = SMALL_WAVES;

    vec3 normal = vec3(0.0);
    normal.xy += 2.0 * (texture2D(normalMap,normalCoords(UV, 0.003, waterTimer, vec2(-0.003, -0.002), normal.xy)).rg - 0.5) * sampleMult * bigWaves.x
               + 2.0 * (texture2D(normalMap,normalCoords(UV, 0.006, waterTimer, vec2(-0.002,  0.003), normal.xy)).rg - 0.5) * sampleMult * bigWaves.y;
    normal.xy += 2.0 * (texture2D(normalMap,normalCoords(UV, 0.05,  waterTimer, vec2( 0.012,  0.025), normal.xy)).rg - 0.5) * sampleMult * rainWaves.x
               + 2.0 * (texture2D(normalMap,normalCoords(UV, 0.08,  waterTimer, vec2(-0.020,  0.012), normal.xy)).rg - 0.5) * sampleMult * rainWaves.y;
    normal.xy += 2.0 * (texture2D(normalMap,normalCoords(UV, 0.18,  waterTimer, vec2(-0.045, -0.033), normal.xy)).rg - 0.5) * sampleMult * smallWaves.x
               + 2.0 * (texture2D(normalMap,normalCoords(UV, 0.3,   waterTimer, vec2( 0.033, -0.045), normal.xy)).rg - 0.5) * sampleMult * smallWaves.y
               + 2.0 * (texture2D(normalMap,normalCoords(UV, 0.2,   waterTimer, vec2(-0.033,  0.055), normal.xy)).rg - 0.5) * sampleMult * smallWaves.z
               + 2.0 * (texture2D(normalMap,normalCoords(UV, 0.32,  waterTimer, vec2( 0.055,  0.033), normal.xy)).rg - 0.5) * sampleMult * smallWaves.w;

    float distToCenter = length(rippleMapUV - vec2(0.5));
    float blendClose = mix(0.2, 1.0, smoothstep(10, 70, radialDepth));
    float blendFar = 1.0 - smoothstep(0.3, 0.4, distToCenter);
    float distortionLevel = 3.5;
    vec2 actorRipple = distortionLevel * texture2D(rippleMap, rippleMapUV).ba * blendFar * blendClose;

    vec4 rainRipple;

    if (rainIntensity > 0.01 && enableRainRipples)
        rainRipple = 2.0 * rainCombined(position.xy / 1300.0 + actorRipple * 0.01, waterTimer) * clamp(rainIntensity, 0.0, 1.0);
    else
        rainRipple = vec4(0.0);

    normal = normalize(vec3(normal.xy * BUMP + actorRipple.xy + rainRipple.xy, 1.0));

    if (cameraPos.z < 0.0)
        normal = -normal;

    // simple rain ripples
    vec3 simpleRain = vec3(rainRipple.w * length(gl_LightModel.ambient.xyz) * 0.08);

    vec4 sunSpec = lcalcSpecular(0);
    // alpha component is sun visibility; we want to start fading lighting effects when visibility is low
    sunSpec.a = min(1.0, sunSpec.a / SUN_SPEC_FADING_THRESHOLD);

    // not really specular, just sun reflection
    float sunDisk = clamp(dot(reflect(viewDir, normalize(vec3(normal.xy * distortMult * REFL_BUMP, normal.z))), sunWorldDir), 0.0, 1.0);
    float sunFade = sunSpec.a * (2 - sunWorldDir.z);
    vec3 specular = pow(sunDisk, SPEC_HARDNESS) * sunFade * sunSpec.rgb;

    // align normal x-axis with viewspace x-axis before doing distortion sampling
    // this would break if the player could look precisely up or down
    vec2 viewAxis = normalize((gl_ModelViewMatrixInverse * vec4(1.0, 0.0, 0.0, 0.0)).xy);
    mat2 rotateMatrix = mat2(viewAxis, vec2(-viewAxis.y, viewAxis.x));

    vec2 screenCoordsOffset = normal.xy * rotateMatrix * distortMult;

    // this makes almost no visible difference with default values, but Morrowind does it
    normal *= BUMP_FADE_COLOR;

    // replicate Morrowind's fresnel, which calculates inverse alpha
    float normalDot = 0.5 * dot(-viewDir, normal) + 0.5;
    float fresnel = 1.0 - clamp(normalDot * normalDot - ALPHA_REDUCE, 0.0, 1.0);

#if @waterRefraction
    float depthSample = linearizeDepth(sampleRefractionDepthMap(screenCoords), near, far);
    float surfaceDepth = linearizeDepth(gl_FragCoord.z, near, far);
    float realWaterDepth = depthSample - surfaceDepth;  // undistorted water depth in view direction, independent of frustum
    screenCoordsOffset *= clamp(realWaterDepth / BUMP_SUPPRESS_DEPTH, 0.0, 1.0);

    // refraction
    vec3 refraction = sampleRefractionMap(screenCoords - screenCoordsOffset * REFR_BUMP).rgb;
#endif

    // reflection
    vec3 waterColor = LERP_CLOSE_COLOR;
    waterColor *= mix(vec3(1.0), gl_LightModel.ambient.xyz * BASE_AMBIENT, LERP_CLOSE_AMBIENT);

    vec3 reflection = sampleReflectionMap(screenCoords + screenCoordsOffset * REFL_BUMP).rgb;
    reflection = min(reflection + specular, vec3(1.0)) * ENV_REDUCE_COLOR;
    reflection = mix(reflection, waterColor, abs(viewDir.z) * LERP_CLOSE_INTENSITY) + simpleRain;

#if @waterRefraction

#if @wobblyShores
    // wobbly water: hard-fade into refraction texture at extremely low depth, with a wobble based on normal mapping
    float viewFactor = mix(abs(viewDir.z), 1.0, 0.2);
    float verticalWaterDepth = realWaterDepth * viewFactor; // an estimate
    float shoreOffset = verticalWaterDepth - 0.5 - (abs(normal.r) + abs(normal.g)) * 8.0;
    shoreOffset = clamp(mix(shoreOffset * 0.3, 1.0, clamp(linearDepth / WOBBLY_SHORE_FADE_DISTANCE, 0.0, 1.0)), 0.0, 1.0);
    fresnel *= shoreOffset;
#endif

    gl_FragData[0].rgb = mix(refraction, reflection, fresnel);
    gl_FragData[0].a = 1.0;
#else
    gl_FragData[0].rgb = reflection;
    gl_FragData[0].a = fresnel;
#endif

    gl_FragData[0] = applyFogAtDist(gl_FragData[0], radialDepth, linearDepth, far);

#if !@disableNormals
    gl_FragData[1].rgb = normalize(gl_NormalMatrix * normal) * 0.5 + 0.5;
#endif

    applyShadowDebugOverlay();
}
