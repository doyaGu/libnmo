/* Synthetic linear/TCB scale-axis captures from Ballanced's LinearReferenceSamples.h
 * and TCBReferenceSamples.h, produced by the original Win32 DLL.
 * CK2_3D.dll SHA-256: 5c8fc5c5c491ef347df24d98813584865bef240e66dbd325b26eb576cfecc590.
 * VxMath.dll SHA-256: bd17dddb747c943e6e090471305aeda7f87cfbca401b3fada36be400285973a2. */
#ifndef NMO_TEST_ENGINE_SCALEAXIS_SAMPLES_H
#define NMO_TEST_ENGINE_SCALEAXIS_SAMPLES_H
#include <stdbool.h>
#include <stddef.h>

typedef struct axis_sample { float time, value[4]; } axis_sample_t;
typedef struct axis_reference {
    const char *name;
    bool tcb;
    const float *keys;
    size_t count;
    const axis_sample_t *samples;
    size_t sample_count;
} axis_reference_t;

static const float LinearQuaternionSingleKeys[] = {
    2.0f, -0.6f, 0.0f, 0.0f, -0.8f,
};
static const axis_sample_t LinearQuaternionSingleSamples[] = {
    {-1.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {0.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {1.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {2.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {2.00001001f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {3.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {5.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {9.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {9.00000954f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {15.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {20.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
    {21.0f, {-0.600000024f, 0.0f, 0.0f, -0.800000012f}},
};

static const float LinearQuaternionOppositeSignsKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    2.0f, -0.6f, 0.0f, 0.0f, -0.8f,
    9.0f, 0.0f, 0.8f, 0.0f, 0.6f,
    20.0f, 0.0f, 0.0f, 0.6f, -0.8f,
};
static const axis_sample_t LinearQuaternionOppositeSignsSamples[] = {
    {-1.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {1.0f, {0.316227764f, 0.0f, 0.0f, 0.948683262f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {2.00001001f, {-0.599999487f, -1.3960148e-06f, 0.0f, -0.80000037f}},
    {3.0f, {-0.543009102f, -0.138869748f, 0.0f, -0.828164399f}},
    {5.0f, {-0.392653346f, -0.40372774f, 0.0f, -0.826333642f}},
    {9.0f, {0.0f, -0.799999952f, 0.0f, -0.599999964f}},
    {9.00000954f, {0.0f, 0.799999595f, -6.34552237e-07f, 0.60000056f}},
    {15.0f, {0.0f, 0.426297009f, -0.376938045f, 0.822306812f}},
    {20.0f, {0.0f, 0.0f, 0.600000024f, -0.800000012f}},
    {21.0f, {0.0f, 0.0f, 0.600000024f, -0.800000012f}},
};

static const float LinearQuaternionRepeatedTimesKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    2.0f, 0.6f, 0.0f, 0.0f, 0.8f,
    2.0f, 0.0f, 0.8f, 0.0f, 0.6f,
    20.0f, 0.0f, 0.0f, -0.6f, 0.8f,
};
static const axis_sample_t LinearQuaternionRepeatedTimesSamples[] = {
    {-1.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {1.0f, {0.316227764f, 0.0f, 0.0f, 0.948683262f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {2.00001001f, {0.0f, 0.799999714f, -4.07170972e-07f, 0.600000322f}},
    {3.0f, {0.0f, 0.772578299f, -0.0406379327f, 0.63361764f}},
    {5.0f, {0.0f, 0.709651649f, -0.121339925f, 0.694025278f}},
    {9.0f, {0.0f, 0.554764152f, -0.276487887f, 0.784723639f}},
    {9.00000954f, {0.0f, 0.554763734f, -0.276488245f, 0.784723759f}},
    {15.0f, {0.0f, 0.267104536f, -0.477527678f, 0.83703196f}},
    {20.0f, {0.0f, 0.0f, -0.600000024f, 0.800000012f}},
    {21.0f, {0.0f, 0.0f, -0.600000024f, 0.800000012f}},
};

static const float TCBQuaternionTwoKeysKeys[] = {
    2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    10.0f, 0.0f, 0.0f, 0.6f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};
static const axis_sample_t TCBQuaternionTwoKeysSamples[] = {
    {2.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {3.0f, {0.0f, 0.0f, 0.0803509206f, 0.996766567f}},
    {4.0f, {0.0f, 0.0f, 0.160182238f, 0.987087429f}},
    {6.0f, {0.0f, 0.0f, 0.316227764f, 0.948683262f}},
    {8.0f, {0.0f, 0.0f, 0.464106679f, 0.885779262f}},
    {9.0f, {0.0f, 0.0f, 0.533779204f, 0.845623851f}},
    {10.0f, {0.0f, 0.0f, 0.600000024f, 0.800000012f}},
};

static const float TCBQuaternionUnevenTCBKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.5f, -0.25f, 0.4f, 0.2f,
    2.0f, 0.6f, 0.0f, 0.0f, 0.8f, -0.35f, -0.6f, 0.4f, 0.3f, 0.1f,
    9.0f, 0.0f, 0.8f, 0.0f, 0.6f, 0.5f, 0.7f, -0.5f, 0.2f, 0.4f,
    20.0f, 0.0f, 0.0f, -0.6f, 0.8f, -0.2f, -0.3f, 0.6f, 0.3f, 0.2f,
};
static const axis_sample_t TCBQuaternionUnevenTCBSamples[] = {
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.5f, {0.0982256532f, -0.00428579934f, 0.0f, 0.994159818f}},
    {1.0f, {0.304449081f, -0.0173970032f, 0.0f, 0.951612592f}},
    {1.5f, {0.517681897f, -0.0129548907f, 0.0f, 0.854988217f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {3.75f, {0.620028913f, 0.0107297152f, -0.0233365521f, 0.784158409f}},
    {5.5f, {0.623291731f, -0.104257494f, -0.0822085962f, 0.770635664f}},
    {7.25f, {-0.0613461584f, 0.861655414f, 0.0634925589f, 0.499755353f}},
    {9.0f, {0.0f, 0.799999952f, 0.0f, 0.599999964f}},
    {11.75f, {-0.0166088194f, 0.794306993f, -0.0248954184f, 0.606779039f}},
    {14.5f, {-0.0276239533f, 0.642623723f, -0.216890365f, 0.734322965f}},
    {17.25f, {-0.00481365109f, 0.222380117f, -0.505136967f, 0.833882809f}},
    {20.0f, {0.0f, 0.0f, -0.600000024f, 0.800000012f}},
};

static const float TCBQuaternionOppositeSignsKeys[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.5f, -0.25f, 0.0f, 0.0f,
    2.0f, -0.6f, 0.0f, 0.0f, -0.8f, -0.35f, -0.6f, 0.4f, 0.0f, 0.0f,
    9.0f, 0.0f, 0.8f, 0.0f, 0.6f, 0.5f, 0.7f, -0.5f, 0.0f, 0.0f,
    20.0f, 0.0f, 0.0f, 0.6f, -0.8f, -0.2f, -0.3f, 0.6f, 0.0f, 0.0f,
};
static const axis_sample_t TCBQuaternionOppositeSignsSamples[] = {
    {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
    {0.5f, {0.126505479f, -0.00626556389f, 0.0f, 0.990947306f}},
    {1.0f, {0.282519698f, -0.0164327938f, 0.0f, 0.958338678f}},
    {1.5f, {0.447208166f, -0.0179773644f, 0.0f, 0.893624306f}},
    {2.0f, {0.599999964f, 0.0f, 0.0f, 0.799999952f}},
    {3.75f, {-0.622096896f, -0.00395567715f, 0.0259499084f, -0.782500088f}},
    {5.5f, {-0.628091156f, 0.103040963f, 0.0764686167f, -0.767487109f}},
    {7.25f, {0.110712335f, -0.884530663f, -0.0810278207f, -0.445850611f}},
    {9.0f, {0.0f, -0.799999952f, 0.0f, -0.599999964f}},
    {11.75f, {-0.026770249f, 0.761697471f, -0.0801176131f, 0.642402947f}},
    {14.5f, {-0.0261132084f, 0.611430764f, -0.245927155f, 0.75165832f}},
    {17.25f, {-0.0103552332f, 0.341284633f, -0.440927923f, 0.830060363f}},
    {20.0f, {0.0f, 0.0f, 0.600000024f, -0.800000012f}},
};

static const axis_reference_t AxisReferences[] = {
    {"LinearQuaternionSingle", false, LinearQuaternionSingleKeys,
     sizeof(LinearQuaternionSingleKeys) / (5 * sizeof(float)),
     LinearQuaternionSingleSamples, sizeof(LinearQuaternionSingleSamples) / sizeof(axis_sample_t)},
    {"LinearQuaternionOppositeSigns", false, LinearQuaternionOppositeSignsKeys,
     sizeof(LinearQuaternionOppositeSignsKeys) / (5 * sizeof(float)),
     LinearQuaternionOppositeSignsSamples, sizeof(LinearQuaternionOppositeSignsSamples) / sizeof(axis_sample_t)},
    {"LinearQuaternionRepeatedTimes", false, LinearQuaternionRepeatedTimesKeys,
     sizeof(LinearQuaternionRepeatedTimesKeys) / (5 * sizeof(float)),
     LinearQuaternionRepeatedTimesSamples, sizeof(LinearQuaternionRepeatedTimesSamples) / sizeof(axis_sample_t)},
    {"TCBQuaternionTwoKeys", true, TCBQuaternionTwoKeysKeys,
     sizeof(TCBQuaternionTwoKeysKeys) / (10 * sizeof(float)),
     TCBQuaternionTwoKeysSamples, sizeof(TCBQuaternionTwoKeysSamples) / sizeof(axis_sample_t)},
    {"TCBQuaternionUnevenTCB", true, TCBQuaternionUnevenTCBKeys,
     sizeof(TCBQuaternionUnevenTCBKeys) / (10 * sizeof(float)),
     TCBQuaternionUnevenTCBSamples, sizeof(TCBQuaternionUnevenTCBSamples) / sizeof(axis_sample_t)},
    {"TCBQuaternionOppositeSigns", true, TCBQuaternionOppositeSignsKeys,
     sizeof(TCBQuaternionOppositeSignsKeys) / (10 * sizeof(float)),
     TCBQuaternionOppositeSignsSamples, sizeof(TCBQuaternionOppositeSignsSamples) / sizeof(axis_sample_t)},
};

#endif
