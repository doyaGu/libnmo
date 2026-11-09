/**
 * @file terratools.h
 * @brief The Terratools managers Ballance's blocks use: the time manager,
 *        the database manager and the debug manager.
 */

#ifndef NMO_BB_TERRATOOLS_INTERNAL_H
#define NMO_BB_TERRATOOLS_INTERNAL_H

#include "../bb_sdk.h"
#include "bb/nmo_bb_terratools.h"

/* A particle (Particle), in world space */
typedef struct eng_particle {
    float pos[3], dir[3];
    float color[4], dcolor[4];
    float size, dsize;
    float angle, dangle;
    float life;
    int32_t frame;
    float frame_time, dframe_time;
    float dt;             /* the time it last moved by (m_DeltaTime) */
    float bounce;         /* m_Bounce: how much a deflector sends it back */
    float weight;         /* m_Weight: how much gravity and tunnels pull it */
    float surface;        /* m_Surface: how much the winds and the atmosphere take it */
} eng_particle_t;

/* A particle's impact on a deflector: where, the deflector's normal there, which, and the
   texture coordinates (a plane's and an object's) */
typedef struct eng_impact {
    float position[3], direction[3];
    nmo_object_id_t object;
    float uv[2];
} eng_impact_t;

/* A particle system's emitter (ParticleEmitter): its particles, and how they draw */
typedef struct eng_emitter {
    int32_t node;
    nmo_object_id_t entity;
    nmo_object_id_t texture;
    int32_t render_mode;
    int32_t source_blend, dest_blend;
    int32_t frame_count;
    uint32_t random;      /* the emitter's own rand() */
    bool shown;           /* ShowParticles */
    uint64_t updated;     /* the engine frame + 1 it last moved its particles in */
    /* TimePointEmitter: where the entity was at its last emission, and the time since */
    float trail_pos[3];
    float trail_time;
    bool trail_started;
    ENG_VEC(eng_particle_t) particles;
    /* The deflector impacts of its last update (ParticleImpact), and the next one to output */
    ENG_VEC(struct eng_impact) impacts;
    size_t current_impact;
} eng_emitter_t;

/* A TT Simple Shadow: its material, projected on the floors under its object */
typedef struct eng_shadow {
    int32_t node;
    nmo_object_id_t entity;
    nmo_object_id_t texture;
    eng_floor_shadow_t floor;
    uint64_t ran;          /* the engine frame after its block last ran (its temporary callback) */
} eng_shadow_t;

/* A TT Sky: its mesh around the camera, drawn first with its own projection */
typedef struct eng_sky {
    int32_t node;
    nmo_object_id_t mesh;
    nmo_object_id_t orientation;
    float distortion;
    bool shown;
} eng_sky_t;

/* A band of TT_MotorSound's sounds, as a line of TT_LoadMotorSettings' file has it: the speeds
   it plays at and the pitch over them, and where its volume fades */
typedef struct eng_motor_band {
    float pitch_max, pitch_min;
    float speed_max, speed_min;
    float fade_in, fade_out;
} eng_motor_band_t;

/* TT_InterfaceManager_RT's CGameInfo: one at most, made by TT Create new GameInfo */
typedef struct eng_game_info {
    int32_t handle; /* its "Address of GameInfo (hex)": the engine numbers them from 1 */
    char *game_name, *level_name;
    char *reg_subkey; /* the registry blocks' keys start with it */
    int32_t game_score, level_score, game_id, level_id, game_bonus, level_bonus, level_reached;
} eng_game_info_t;

/* A value of a CNemoArray: a cell of the array's first row, as its column's type keeps it */
typedef struct eng_nemo_element {
    int32_t type; /* CK_ARRAYTYPE */
    uint32_t value;
    char *text;
    void *data; /* a parameter cell's value */
    size_t size;
    nmo_guid_t parameter_type;
} eng_nemo_element_t;

/* CNemoArray: an array's first row kept across CMOs, by the CMO's name and the array's */
typedef struct eng_nemo_array {
    char *cmo, *name;
    ENG_VEC(eng_nemo_element_t) elements;
} eng_nemo_array_t;

typedef struct eng_tt {
    float time;   /* TT Time Manager: TT_Timer's time, and whether it runs */
    bool timing;
    char *database_file; /* TT Database Manager: TT Set Database Properties */
    bool database_crypted;
    ENG_VEC(char *) database_arrays; /* the arrays it saves (DatabaseManager::Register) */
    bool debug_mode; /* CTTDebugManager::m_DebugMode, which TT_DebugON sets */
    bool chrono_on;  /* TT_Chrono's chronoOn, one for every chronometer */
    eng_map_t video_textures; /* the textures TT_PreloadTextures put in video memory */
    ENG_VEC(eng_sky_t) skies;
    ENG_VEC(eng_shadow_t) shadows;
    ENG_VEC(int32_t) real_shadows; /* the TT RealShadowMapping blocks with this frame's callback */
    ENG_VEC(eng_emitter_t) emitters; /* ParticleManager's */
    /* TT_Toolbox_RT's motor settings, which every TT_MotorSound shares: the file's Volume, the
       last GlobalVolume a motor sound read, and each sound's band */
    float motor_volume, motor_global_volume;
    eng_motor_band_t motor_bands[3];
    /* TT_InterfaceManager_RT's manager: the game info, the CMO TT Load CMO named, the task switch
       and rookie flags and the installed arrays */
    eng_game_info_t *game_info;
    int32_t game_infos_made;
    char *cmo_name;
    bool task_switch, rookie;
    ENG_VEC(eng_nemo_array_t) nemo_arrays;
} eng_tt_t;

/* database.c: the Terratools managers' state, added once with the first TT DLL;
   eng_tt() is NULL before */
nmo_status_t eng_tt_add(nmo_engine_t *engine);
eng_tt_t *eng_tt(const nmo_engine_t *engine);

/* interface_manager.c: what the interface manager keeps goes */
void eng_free_tt_interface(nmo_engine_t *engine);

/* particles.c */
/* The particles of the emitters shown, as their render modes draw them */
void eng_draw_particles(nmo_engine_t *engine, const float view[16]);
void eng_free_emitters(nmo_engine_t *engine);

/* gravity.c */
/* The shadows' pre render callbacks: each projected on the floors under its object */
void eng_update_shadows(nmo_engine_t *engine);
/* The skies TT Sky shows, placed at the camera: the frame's first draws */
void eng_draw_skies(nmo_engine_t *engine, const float view[16]);

/* Each file registers the blocks of one TT DLL */
nmo_status_t eng_register_tt_database(nmo_engine_t *engine);
nmo_status_t eng_register_tt_interface_manager(nmo_engine_t *engine);
nmo_status_t eng_register_tt_toolbox(nmo_engine_t *engine);
nmo_status_t eng_register_tt_gravity(nmo_engine_t *engine);
nmo_status_t eng_register_tt_particles(nmo_engine_t *engine);

#endif /* NMO_BB_TERRATOOLS_INTERNAL_H */
