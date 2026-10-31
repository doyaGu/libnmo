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
} eng_particle_t;

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
} eng_emitter_t;

/* A TT Simple Shadow: its material, projected on the floors under its object */
#define ENG_SHADOW_FLOORS 50
typedef struct eng_shadow {
    int32_t node;
    nmo_object_id_t entity;
    nmo_object_id_t material;
    nmo_object_id_t texture;
    nmo_object_id_t floors[ENG_SHADOW_FLOORS];
    int32_t floor_count;
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

typedef struct eng_tt {
    float time;   /* TT Time Manager: TT_Timer's time, and whether it runs */
    bool timing;
    char *database_file; /* TT Database Manager: TT Set Database Properties */
    bool database_crypted;
    ENG_VEC(char *) database_arrays; /* the arrays it saves (DatabaseManager::Register) */
    bool debug_mode; /* CTTDebugManager::m_DebugMode, which TT_DebugON sets */
    eng_map_t video_textures; /* the textures TT_PreloadTextures put in video memory */
    ENG_VEC(eng_sky_t) skies;
    ENG_VEC(eng_shadow_t) shadows;
    ENG_VEC(eng_emitter_t) emitters; /* ParticleManager's */
} eng_tt_t;

/* database.c: the Terratools managers' state, added once with the first TT DLL;
   eng_tt() is NULL before */
nmo_status_t eng_tt_add(nmo_engine_t *engine);
eng_tt_t *eng_tt(const nmo_engine_t *engine);

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
