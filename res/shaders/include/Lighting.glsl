// Shared direct-lighting code.
// Light types only produce a LightSample (where the light comes from and how much
// arrives); ShadeBRDF is the single place that decides how the surface reflects it.

#ifndef TS_LIGHTING_GLSL
#define TS_LIGHTING_GLSL

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2
#define LIGHT_AREA        3

#define AREA_SHAPE_RECT   0
#define AREA_SHAPE_DISK   1

#define PI         3.14159265
// Must match DeferredLightPass::GPULight member order (std430).
struct Light
{
    vec4 directionAmbient;  // xyz = direction, w = ambient
    vec4 colorIntensity;    // rgb = color,     a = intensity
    vec4 positionType;      // xyz = position,  w = type
    vec4 params;            // Point: radius, falloff / Spot: radius, falloff, innerDeg, outerDeg
                            // Area: width, height, shape (rect: full size, disk: width = diameter)
};

struct Surface
{
    vec3  P;
    vec3  N;
    vec3  V;
    vec3  diffuseColor;
    vec3  specularColor;
    float shininess;
};

struct LightSample
{
    vec3  L;             // direction used for diffuse
    vec3  specL;         // direction used for specular (area: representative point)
    vec3  radiance;      // color * intensity * attenuation
    float diffuseTerm;   // cosine / form factor applied to diffuse
    float specularTerm;  // extra energy factor applied to specular
};

int LightType(Light l)
{
    return int(l.positionType.w + 0.5);
}

vec3 LightRadiance(Light l)
{
    return l.colorIntensity.rgb * l.colorIntensity.a;
}

float DistanceAttenuation(float dist, float radius)
{
    float f = clamp(1.0 - pow(dist / max(radius, 1e-4), 4.0), 0.0, 1.0);
    return f * f / (dist * dist + 1.0);
}

// ---------------------------------------------------------------------------
// Punctual lights
// ---------------------------------------------------------------------------

bool SampleDirectional(Light l, Surface s, out LightSample o)
{
    o.L            = normalize(-l.directionAmbient.xyz);
    o.specL        = o.L;
    o.radiance     = LightRadiance(l);
    o.diffuseTerm  = max(dot(s.N, o.L), 0.0);
    o.specularTerm = 1.0;
    return o.diffuseTerm > 0.0;
}

bool SamplePoint(Light l, Surface s, out LightSample o)
{
    vec3  toLight = l.positionType.xyz - s.P;
    float dist    = length(toLight);
    float atten   = DistanceAttenuation(dist, l.params.x);

    o.L            = toLight / max(dist, 1e-4);
    o.specL        = o.L;
    o.radiance     = LightRadiance(l) * atten;
    o.diffuseTerm  = max(dot(s.N, o.L), 0.0);
    o.specularTerm = 1.0;
    return atten > 0.0 && o.diffuseTerm > 0.0;
}

bool SampleSpot(Light l, Surface s, out LightSample o)
{
    if (!SamplePoint(l, s, o))
        return false;

    float cosInner = cos(radians(l.params.z));
    float cosOuter = cos(radians(l.params.w));
    float theta    = dot(-o.L, normalize(l.directionAmbient.xyz));
    float cone     = smoothstep(cosOuter, cosInner, theta);

    o.radiance *= cone;
    return cone > 0.0;
}

// ---------------------------------------------------------------------------
// Area lights (one-sided, emitting along directionAmbient.xyz)
// ---------------------------------------------------------------------------

// The basis has no roll control: the light's right axis is derived from world up.
void BuildLightBasis(vec3 n, out vec3 right, out vec3 up)
{
    vec3 ref = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    right = normalize(cross(ref, n));
    up    = cross(n, right);
}

// Form factor of a sphere subtending sin^2(sigma) = sinSigmaSqr, centred cosTheta
// away from N, clipped to the horizon (Frostbite, "Moving Frostbite to PBR", 2014).
// Exact for spheres; for other shapes it is used with their vector form factor and
// is exact whenever the light is fully above the horizon.
float HorizonClippedFormFactor(float cosTheta, float sinSigmaSqr)
{
    sinSigmaSqr = clamp(sinSigmaSqr, 1e-6, 0.9999);
    if (cosTheta * cosTheta > sinSigmaSqr)
        return sinSigmaSqr * max(cosTheta, 0.0);

    float sinTheta      = sqrt(1.0 - cosTheta * cosTheta);
    float x             = sqrt(1.0 / sinSigmaSqr - 1.0);
    float y             = clamp(-x * cosTheta / sinTheta, -1.0, 1.0);
    float sinThetaSqrtY = sinTheta * sqrt(1.0 - y * y);
    float e = (cosTheta * acos(y) - x * sinThetaSqrtY) * sinSigmaSqr + atan(sinThetaSqrtY / x);
    return max(e / PI, 0.0);
}

// One edge of the vector form factor, 1/(2*pi) folded in. Rational fit of
// theta/sin(theta) (Heitz et al., LTC 2016) — stable for short and near-opposite edges.
vec3 EdgeVectorFormFactor(vec3 v1, vec3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaSinTheta = x > 0.0 ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v2, v1) * thetaSinTheta;
}

// Polygon vertices must wind counter-clockwise in the light's (right, up) basis.
float VectorFormFactorToScalar(vec3 F, vec3 N)
{
    float len = length(F);
    if (len < 1e-6)
        return 0.0;
    return HorizonClippedFormFactor(dot(F, N) / len, len);
}

float RectFormFactor(vec3 P, vec3 N, vec3 c, vec3 R, vec3 U, vec2 halfSize)
{
    vec3 v0 = normalize(c - R * halfSize.x - U * halfSize.y - P);
    vec3 v1 = normalize(c + R * halfSize.x - U * halfSize.y - P);
    vec3 v2 = normalize(c + R * halfSize.x + U * halfSize.y - P);
    vec3 v3 = normalize(c - R * halfSize.x + U * halfSize.y - P);

    vec3 F = EdgeVectorFormFactor(v0, v1) + EdgeVectorFormFactor(v1, v2)
           + EdgeVectorFormFactor(v2, v3) + EdgeVectorFormFactor(v3, v0);
    return VectorFormFactorToScalar(F, N);
}

// The disk is integrated as an equal-area regular polygon; 8 sides stays within
// ~1% of the exact disk, including large disks close to the surface.
#define AREA_DISK_SEGMENTS 8

float DiskFormFactor(vec3 P, vec3 N, vec3 c, vec3 R, vec3 U, float radius)
{
    const float stepAngle = 2.0 * PI / float(AREA_DISK_SEGMENTS);
    float circumRadius = radius * sqrt(stepAngle / sin(stepAngle));

    vec3 first = normalize(c + R * circumRadius - P);
    vec3 prev  = first;
    vec3 F     = vec3(0.0);
    for (int k = 1; k < AREA_DISK_SEGMENTS; ++k)
    {
        float a   = stepAngle * float(k);
        vec3  cur = normalize(c + (R * cos(a) + U * sin(a)) * circumRadius - P);
        F   += EdgeVectorFormFactor(prev, cur);
        prev = cur;
    }
    F += EdgeVectorFormFactor(prev, first);
    return VectorFormFactorToScalar(F, N);
}

vec3 AreaRepresentativePoint(vec3 P, vec3 r, vec3 c, vec3 lightN, vec3 R, vec3 U,
                             vec2 halfSize, int shape)
{
    float denom = dot(r, lightN);
    vec3  hit;
    if (denom < -1e-4)
    {
        hit = P + r * (dot(c - P, lightN) / denom);
    }
    else
    {
        hit = P + r * 1000.0;
        hit -= lightN * dot(hit - c, lightN);
    }

    vec3 d  = hit - c;
    vec2 uv = vec2(dot(d, R), dot(d, U));
    if (shape == AREA_SHAPE_DISK)
    {
        float len = length(uv);
        if (len > halfSize.x)
            uv *= halfSize.x / len;
    }
    else
    {
        uv = clamp(uv, -halfSize, halfSize);
    }
    return c + R * uv.x + U * uv.y;
}

bool SampleArea(Light l, Surface s, out LightSample o)
{
    vec3 c      = l.positionType.xyz;
    vec3 lightN = normalize(l.directionAmbient.xyz);
    if (dot(s.P - c, lightN) <= 0.0)
        return false;

    vec3 R, U;
    BuildLightBasis(lightN, R, U);

    int   shape    = int(l.params.z + 0.5);
    vec2  halfSize = max(l.params.xy * 0.5, vec2(1e-3));
    float area;
    if (shape == AREA_SHAPE_DISK)
    {
        halfSize.y    = halfSize.x;
        area          = PI * halfSize.x * halfSize.x;
        o.diffuseTerm = DiskFormFactor(s.P, s.N, c, R, U, halfSize.x);
    }
    else
    {
        area          = 4.0 * halfSize.x * halfSize.y;
        o.diffuseTerm = RectFormFactor(s.P, s.N, c, R, U, halfSize);
    }

    vec3  r   = reflect(-s.V, s.N);
    vec3  toS = AreaRepresentativePoint(s.P, r, c, lightN, R, U, halfSize, shape) - s.P;
    float ds2 = max(dot(toS, toS), 1e-6);

    o.specL        = toS * inversesqrt(ds2);
    o.L            = normalize(c - s.P);
    o.radiance     = LightRadiance(l);
    o.specularTerm = max(dot(lightN, -o.specL), 0.0) * area / (ds2 + area);
    return o.diffuseTerm > 0.0 || o.specularTerm > 0.0;
}

// ---------------------------------------------------------------------------
// Dispatch + BRDF
// ---------------------------------------------------------------------------

bool SampleLight(Light l, Surface s, out LightSample o)
{
    switch (LightType(l))
    {
    case LIGHT_DIRECTIONAL: return SampleDirectional(l, s, o);
    case LIGHT_POINT:       return SamplePoint(l, s, o);
    case LIGHT_SPOT:        return SampleSpot(l, s, o);
    case LIGHT_AREA:        return SampleArea(l, s, o);
    }
    return false;
}

// Lambert diffuse + Blinn-Phong specular. Swap this for Cook-Torrance when moving to PBR.
vec3 ShadeBRDF(Surface s, LightSample ls)
{
    vec3  diffuse  = s.diffuseColor * ls.diffuseTerm;
    vec3  H        = normalize(ls.specL + s.V);
    float spec     = pow(max(dot(s.N, H), 0.0), s.shininess);
    vec3  specular = s.specularColor * spec * ls.specularTerm;
    return (diffuse + specular) * ls.radiance;
}

// Constant ambient for now; replace with IBL / DDGI irradiance later.
vec3 EvaluateAmbient(Surface s, vec3 ambient)
{
    return ambient * s.diffuseColor;
}

#endif // TS_LIGHTING_GLSL
