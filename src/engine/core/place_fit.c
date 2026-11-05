#include "../engine_internal.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_type_query.h"

#include <math.h>

typedef struct fit_points { eng_vector_t *data; size_t count, capacity; } fit_points_t;
typedef struct fit_bounds { eng_vector_t min, max; bool valid; } fit_bounds_t;
typedef struct fit_key { float key; eng_vector_t point; } fit_key_t;

static float coordinate(eng_vector_t p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
static double dot(eng_vector_t a, eng_vector_t b) { return (double)a.x*b.x+(double)a.y*b.y+(double)a.z*b.z; }

static bool bound(fit_bounds_t *box, eng_vector_t p)
{
    if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z)) return false;
    if (!box->valid) { box->min = box->max = p; box->valid = true; }
    else {
        box->min = (eng_vector_t){fminf(box->min.x,p.x),fminf(box->min.y,p.y),fminf(box->min.z,p.z)};
        box->max = (eng_vector_t){fmaxf(box->max.x,p.x),fmaxf(box->max.y,p.y),fmaxf(box->max.z,p.z)};
    }
    return true;
}

static bool collect(nmo_engine_t *engine, int32_t root, fit_points_t *points, fit_bounds_t *box)
{
    for (int32_t index = root; ; ) {
        nmo_object_id_t id = engine->entities.data[index].id;
        const nmo_3dentity_state_t *entity = nmo_type_query_object_get_ancestor_state_by_guid(engine->types,eng_find_object(engine,id),CKPGUID_3DENTITY);
        nmo_object_id_t mesh_id = entity != NULL ? eng_object_ref(engine,id,&entity->current_mesh) : 0;
        nmo_engine_mesh_t mesh;
        if (eng_map_get(&engine->destroyed,mesh_id) < 0 && nmo_engine_mesh(engine,mesh_id,&mesh)) {
            for (int32_t i = 0; mesh.vertices != NULL && i < mesh.vertex_count; i++) {
                const nmo_vector_t *v = &mesh.vertices[i].position; eng_vector_t local = {v->x,v->y,v->z}, *world = NULL;
                if (!ENG_VEC_PUSH(*points,world)) return false;
                eng_entity_transform(engine,index,world,&local,-1); if (!bound(box,*world)) return false;
            }
        }
        eng_vector_t min, max;
        if (eng_entity_world_box(engine,index,&min,&max) && (!bound(box,min) || !bound(box,max))) return false;
        if (engine->entities.data[index].first_child >= 0) index = engine->entities.data[index].first_child;
        else {
            while (index != root && engine->entities.data[index].next_sibling < 0) index = engine->entities.data[index].parent;
            if (index == root) break;
            index = engine->entities.data[index].next_sibling;
        }
    }
    return true;
}

/* VxEigenMatrix's unsorted tridiagonal/QL basis; eigenvectors are columns. */
static bool principal_axes(const fit_points_t *points, eng_vector_t axes[3])
{
    eng_vector_t origin = points->data[0]; double mean[3] = {0}, scatter[3][3] = {{0}};
    for (size_t i = 0; i < points->count; i++) for (int k = 0; k < 3; k++) mean[k] += (double)coordinate(points->data[i],k)-coordinate(origin,k);
    for (int k = 0; k < 3; k++) mean[k] /= (double)points->count;
    for (size_t i = 0; i < points->count; i++) {
        double delta[3]; for (int k = 0; k < 3; k++) delta[k] = ((double)coordinate(points->data[i],k)-coordinate(origin,k))-mean[k];
        for (int r = 0; r < 3; r++) for (int c = r; c < 3; c++) scatter[r][c] += delta[r]*delta[c];
    }
    double scale = fmax(scatter[0][0],fmax(scatter[1][1],scatter[2][2])); float a[3][3] = {{0}};
    for (int r = 0; r < 3; r++) for (int c = r; c < 3; c++) a[r][c] = scale > 0 ? (float)(scatter[r][c]/scale) : 0;
    double d[3] = {a[0][0],a[1][1],a[2][2]}, e[3] = {a[0][1],a[1][2],0}, basis[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    if (a[0][2] != 0) {
        double length = hypot(a[0][1],a[0][2]), u = a[0][1]/length, v = a[0][2]/length, shift = (a[2][2]-a[1][1])*v+2*a[1][2]*u;
        d[1] = a[1][1]+shift*v; d[2] = a[2][2]-shift*v; e[0] = length; e[1] = a[1][2]-shift*u;
        basis[1][1] = u; basis[1][2] = v; basis[2][1] = v; basis[2][2] = -u;
    }
    for (int i = 0; i < 3; i++) {
        int iteration = 0;
        for (; iteration < 32; iteration++) {
            int m = i;
            while (m < 2) { double sum = fabs(d[m])+fabs(d[m+1]); if (fabs(e[m])+sum == sum) break; m++; }
            if (m == i) break;
            double shift = (d[i+1]-d[i])/(2*e[i]), radius = hypot(shift,1);
            double g = e[i]/(shift >= 0 ? shift+radius : shift-radius)+d[m]-d[i], sine = 1, cosine = 1, correction = 0;
            for (int j = m-1; j >= i; j--) {
                double f = sine*e[j], b = cosine*e[j];
                if (fabs(g) > fabs(f)) { double ratio = f/g, norm = hypot(ratio,1); e[j+1] = norm*g; cosine = 1/norm; sine = ratio*cosine; }
                else { double ratio = g/f, norm = hypot(ratio,1); e[j+1] = f*norm; sine = 1/norm; cosine = ratio*sine; }
                double old = d[j+1]-correction, rotated = (d[j]-old)*sine+2*b*cosine;
                correction = sine*rotated; d[j+1] = old+correction; g = cosine*rotated-b;
                for (int row = 0; row < 3; row++) { double next = basis[row][j+1], previous = basis[row][j]; basis[row][j+1] = cosine*next+sine*previous; basis[row][j] = cosine*previous-sine*next; }
            }
            d[i] -= correction; e[i] = g; e[m] = 0;
        }
        if (iteration == 32) return false;
    }
    for (int i = 0; i < 2; i++) { axes[i] = (eng_vector_t){(float)basis[0][i],(float)basis[1][i],(float)basis[2][i]}; eng_vector_normalize(&axes[i]); }
    axes[2] = eng_vector_cross(&axes[0],&axes[1]); return true;
}

static bool fit_box(const fit_points_t *points, eng_matrix_t *out)
{
    eng_vector_t axes[3]; if (points->count == 0 || !principal_axes(points,axes)) return false;
    eng_vector_t origin = points->data[0]; double low[3] = {0}, high[3] = {0};
    for (size_t i = 1; i < points->count; i++) {
        eng_vector_t delta = {points->data[i].x-origin.x,points->data[i].y-origin.y,points->data[i].z-origin.z};
        for (int k = 0; k < 3; k++) { double p = dot(delta,axes[k]); if (p < low[k]) low[k] = p; if (p > high[k]) high[k] = p; }
    }
    eng_matrix_t box; eng_matrix_identity(&box); double center[3] = {origin.x,origin.y,origin.z};
    for (int k = 0; k < 3; k++) {
        double middle = (low[k]+high[k])*.5, half = (high[k]-low[k])*.5+.1f;
        for (int c = 0; c < 3; c++) { center[c] += middle*coordinate(axes[k],c); box.m[k][c] = (float)(half*coordinate(axes[k],c)); }
    }
    for (int c = 0; c < 3; c++) box.m[3][c] = (float)center[c];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++) if (!(fabsf(box.m[r][c]) <= 1e6f)) return false;
    *out = box; return true;
}

static int compare_keys(const void *a, const void *b)
{
    float x = ((const fit_key_t *)a)->key, y = ((const fit_key_t *)b)->key; return (x > y)-(x < y);
}

static bool near(eng_vector_t a, eng_vector_t b, float radius)
{
    return fabsf(a.x-b.x) <= radius && fabsf(a.y-b.y) <= radius && fabsf(a.z-b.z) <= radius;
}

bool eng_place_best_fit(nmo_engine_t *engine, nmo_object_id_t first, nmo_object_id_t second, eng_matrix_t *out)
{
    int32_t a = eng_entity_index(engine,first), b = eng_entity_index(engine,second);
    if (a < 0 || b < 0 || out == NULL || !nmo_type_registry_is_class_derived_from(engine->types,nmo_engine_object_class(engine,first),NMO_CID_PLACE) || !nmo_type_registry_is_class_derived_from(engine->types,nmo_engine_object_class(engine,second),NMO_CID_PLACE)) return false;
    fit_points_t one = {0}, two = {0}, common = {0}; fit_bounds_t bounds = {0}; fit_key_t *keys = NULL; bool result = false;
    if (!collect(engine,a,&one,&bounds) || !collect(engine,b,&two,&bounds) || one.count == 0 || two.count == 0) goto done;
    float spans[3]; int axis = 0;
    for (int k = 0; k < 3; k++) { spans[k] = (coordinate(bounds.max,k)+.01f)-(coordinate(bounds.min,k)-.01f); if (spans[k] > spans[axis]) axis = k; }
    /* Native grid: 20 cells over at most two world units, with a .1-cell
     * per-coordinate match radius. A sorted sweep avoids its dense 3D allocation. */
    float cell = fminf(spans[axis],2)/20, tolerance = .1f*cell;
    if (!(cell > 0) || !isfinite(cell) || one.count > SIZE_MAX/sizeof(*keys)) goto done;
    keys = malloc(one.count*sizeof(*keys)); if (keys == NULL) goto done;
    for (size_t i = 0; i < one.count; i++) keys[i] = (fit_key_t){coordinate(one.data[i],axis),one.data[i]};
    qsort(keys,one.count,sizeof(*keys),compare_keys);
    for (size_t i = 0; i < two.count; i++) {
        eng_vector_t query = two.data[i]; double key = coordinate(query,axis); size_t lo = 0, hi = one.count;
        while (lo < hi) { size_t mid = lo+(hi-lo)/2; if (keys[mid].key < key-tolerance) lo = mid+1; else hi = mid; }
        bool matched = false;
        for (size_t j = lo; j < one.count && keys[j].key <= key+tolerance; j++) if (near(keys[j].point,query,tolerance)) { matched = true; break; }
        if (!matched) continue;
        bool duplicate = false;
        for (size_t j = 0; j < common.count; j++) if (near(common.data[j],query,.1f)) { duplicate = true; break; }
        if (!duplicate) { eng_vector_t *point = NULL; if (!ENG_VEC_PUSH(common,point)) goto done; *point = query; }
    }
    result = fit_box(&common,out);
done:
    free(keys); free(one.data); free(two.data); free(common.data); return result;
}
