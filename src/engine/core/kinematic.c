/* CKKinematicChain's constrained Jacobian solver and native SVD cutoff. */
#include "../engine_internal.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/nmo_class_ids.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define IK_PI 3.14159265358979323846f

typedef struct ik_body {
    int32_t entity;
    nmo_ik_joint_t joint;
    eng_matrix_t original, trial;
    bool locked, at_limit;
} ik_body_t;

typedef struct ik_workspace {
    int dofs;
    float **rows, **v, *storage;
    float *locked, *active, *angles, *singular, *temporary, *rv;
} ik_workspace_t;

static bool ik_decompose(float **a, int m, int n, float *w, float **v, float *rv1);

static const nmo_kinematicchain_state_t *ik_chain(const nmo_engine_t *engine, nmo_object_id_t chain)
{
    if (engine == NULL || eng_map_get(&engine->destroyed,chain) >= 0) return NULL;
    nmo_object_t *object = eng_find_object(engine,chain);
    return object != NULL && nmo_object_get_class_id(object) == NMO_CID_KINEMATICCHAIN ? nmo_object_get_state(object) : NULL;
}

static nmo_object_id_t ik_live_body(const nmo_engine_t *engine, nmo_object_id_t id)
{
    return eng_entity_index(engine,id) >= 0 && nmo_engine_object_class(engine,id) == NMO_CID_BODYPART ? id : 0;
}

bool eng_kinematic_set_effectors(nmo_engine_t *engine, nmo_object_id_t chain,
    nmo_object_id_t start, nmo_object_id_t end)
{
    if (ik_chain(engine,chain) == NULL) return false;
    int32_t index = eng_map_get(&engine->kinematics.map,chain);
    if (index < 0) {
        index = (int32_t)engine->kinematics.chains.count;
        eng_kinematic_chain_t *entry = NULL;
        if (!ENG_VEC_PUSH(engine->kinematics.chains,entry) || !eng_map_put(&engine->kinematics.map,chain,index)) return false;
    }
    engine->kinematics.chains.data[index] = (eng_kinematic_chain_t){chain,ik_live_body(engine,start),ik_live_body(engine,end)};
    return true;
}

bool eng_kinematic_effectors(const nmo_engine_t *engine, nmo_object_id_t chain,
    nmo_object_id_t *start, nmo_object_id_t *end)
{
    const nmo_kinematicchain_state_t *state = ik_chain(engine,chain);
    if (state == NULL) return false;
    int32_t index = eng_map_get(&engine->kinematics.map,chain);
    const eng_kinematic_chain_t *runtime = index >= 0 ? &engine->kinematics.chains.data[index] : NULL;
    if (start != NULL) *start = ik_live_body(engine,runtime != NULL ? runtime->start : eng_object_ref(engine,chain,&state->start_effector));
    if (end != NULL) *end = ik_live_body(engine,runtime != NULL ? runtime->end : eng_object_ref(engine,chain,&state->end_effector));
    return true;
}

void eng_copy_kinematic_chain(nmo_engine_t *engine, nmo_object_id_t source,
    nmo_object_id_t target, const nmo_object_id_t *originals, const nmo_object_id_t *copies, size_t count)
{
    if (eng_map_get(&engine->kinematics.map,source) < 0) return;
    nmo_object_id_t start = 0, end = 0;
    if (!eng_kinematic_effectors(engine,source,&start,&end)) return;
    nmo_object_id_t remapped_start = start, remapped_end = end;
    for (size_t i = 0; i < count; i++) {
        if (originals[i] == start) remapped_start = copies[i];
        if (originals[i] == end) remapped_end = copies[i];
    }
    eng_kinematic_set_effectors(engine,target,remapped_start,remapped_end);
}

void eng_free_kinematics(nmo_engine_t *engine)
{
    free(engine->kinematics.chains.data); free(engine->kinematics.map.slots);
}

static bool ik_finite(eng_vector_t p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static eng_vector_t ik_difference(eng_vector_t a, eng_vector_t b)
{
    return (eng_vector_t){a.x-b.x,a.y-b.y,a.z-b.z};
}

static float ik_wrap(float angle)
{
    angle = fmodf(angle,2*IK_PI);
    if (angle > IK_PI) angle -= 2*IK_PI;
    if (angle < -IK_PI) angle += 2*IK_PI;
    return angle;
}

static bool ik_workspace_create(ik_workspace_t *w, size_t count)
{
    size_t n = 3*(count-1);
    if (n > INT_MAX || n > (SIZE_MAX/sizeof(float))/(n+9)) return false;
    w->storage = calloc(n*(n+9),sizeof(float));
    w->rows = malloc((n+3)*sizeof(float *));
    if (w->storage == NULL || w->rows == NULL) { free(w->storage); free(w->rows); return false; }
    w->dofs = (int)n; w->v = w->rows+3;
    for (size_t i = 0; i < 3; i++) w->rows[i] = w->storage+i*n;
    for (size_t i = 0; i < n; i++) w->v[i] = w->storage+3*n+i*n;
    w->locked = w->storage+n*(n+3); w->active = w->locked+n;
    w->angles = w->active+n; w->singular = w->angles+n;
    w->temporary = w->singular+n; w->rv = w->temporary+n;
    return true;
}

static bool ik_rotate(nmo_engine_t *engine, ik_body_t *bodies, size_t count,
    ik_workspace_t *w, eng_vector_t delta)
{
    memset(w->locked,0,(size_t)w->dofs*sizeof(float));
    const float target[] = {delta.x,delta.y,delta.z};
    for (int retry = 0; retry <= w->dofs; retry++) {
        eng_vector_t end = eng_entity_position(engine,bodies[count-1].entity,-1);
        bool any = false;
        for (size_t i = 0; i+1 < count; i++) {
            const eng_matrix_t *world = &engine->entities.data[bodies[i].entity].world;
            eng_vector_t arm = ik_difference(end,eng_entity_position(engine,bodies[i].entity,-1));
            for (int axis = 0; axis < 3; axis++) {
                size_t d = 3*i+(size_t)axis;
                bool active = (bodies[i].joint.flags & (1u<<axis)) && !w->locked[d];
                w->active[d] = active; any |= active;
                eng_vector_t direction = {world->m[axis][0],world->m[axis][1],world->m[axis][2]};
                eng_vector_t cross = eng_vector_cross(&direction,&arm);
                w->rows[0][d] = active ? cross.x : 0;
                w->rows[1][d] = active ? cross.y : 0;
                w->rows[2][d] = active ? cross.z : 0;
            }
        }
        if (!any || !ik_decompose(w->rows,3,w->dofs,w->singular,w->v,w->rv)) return false;
        for (int i = 0; i < w->dofs; i++) {
            float sum = 0;
            if (fabsf(w->singular[i]) >= .00001f) {
                for (int row = 0; row < 3; row++) sum += w->rows[row][i]*target[row];
                sum /= w->singular[i];
            }
            w->temporary[i] = sum;
        }
        for (int i = 0; i < w->dofs; i++) {
            float sum = 0;
            for (int j = 0; j < w->dofs; j++) sum += w->v[i][j]*w->temporary[j];
            if (!isfinite(sum)) return false;
            w->angles[i] = sum;
        }
        for (size_t i = 0; i+1 < count; i++) bodies[i].trial = engine->entities.data[bodies[i].entity].local;
        int constrained = -1;
        float largest = -10000;
        for (size_t i = 0; i+1 < count; i++) {
            float euler[3], angles[3] = {0};
            eng_matrix_to_euler(&bodies[i].trial,&euler[0],&euler[1],&euler[2]);
            const float minimum[] = {bodies[i].joint.min.x,bodies[i].joint.min.y,bodies[i].joint.min.z};
            const float maximum[] = {bodies[i].joint.max.x,bodies[i].joint.max.y,bodies[i].joint.max.z};
            for (int axis = 0; axis < 3; axis++) {
                size_t d = 3*i+(size_t)axis;
                if (!w->active[d]) continue;
                angles[axis] = w->angles[d];
                if (!(bodies[i].joint.flags & (16u<<axis))) continue;
                float angle = ik_wrap(euler[axis]+angles[axis]), violation = 0;
                if (angle < minimum[axis]) violation = minimum[axis]-angle;
                if (angle > maximum[axis]) violation = angle-maximum[axis];
                if (violation > 0 && violation > largest) { constrained = (int)d; largest = violation; }
            }
            eng_matrix_t rotation, result;
            eng_matrix_from_euler(&rotation,angles[0],angles[1],angles[2]);
            eng_matrix_multiply(&result,&bodies[i].trial,&rotation);
            eng_entity_set_local_matrix(engine,bodies[i].entity,&result,false);
        }
        if (constrained < 0) return true;
        for (size_t i = 0; i+1 < count; i++) eng_entity_set_local_matrix(engine,bodies[i].entity,&bodies[i].trial,false);
        w->locked[constrained] = 1;
    }
    return false;
}

static bool ik_prepare_body(nmo_engine_t *engine, ik_body_t *body, int32_t entity)
{
    nmo_object_t *object = eng_find_object(engine,engine->entities.data[entity].id);
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_BODYPART) return false;
    const nmo_bodypart_state_t *state = nmo_object_get_state(object);
    body->entity = entity; body->joint = state->rotation_joint;
    body->original = engine->entities.data[entity].local;
    body->locked = (body->joint.flags & 7) == 0;
    float angles[3]; eng_matrix_to_euler(&body->original,&angles[0],&angles[1],&angles[2]);
    for (int axis = 0; axis < 3; axis++) {
        if (!isfinite(angles[axis])) return false;
        if (!(body->joint.flags & (1u<<axis)) || !(body->joint.flags & (16u<<axis))) continue;
        const float minimum[] = {body->joint.min.x,body->joint.min.y,body->joint.min.z};
        const float maximum[] = {body->joint.max.x,body->joint.max.y,body->joint.max.z};
        if (!isfinite(minimum[axis]) || !isfinite(maximum[axis])) return false;
        float angle = ik_wrap(angles[axis]);
        if (angle-.05f <= minimum[axis] || angle+.05f >= maximum[axis]) body->locked = body->at_limit = true;
    }
    return ik_finite(eng_entity_position(engine,entity,-1));
}

static bool ik_project(eng_vector_t origin, eng_vector_t *target, float length, bool rigid)
{
    eng_vector_t delta = ik_difference(*target,origin);
    float distance = eng_vector_magnitude(&delta);
    if (!isfinite(distance) || !isfinite(length)) return false;
    if (distance > length || (rigid && distance < length)) {
        if (distance == 0) return false;
        float scale = length/distance;
        *target = (eng_vector_t){origin.x+delta.x*scale,origin.y+delta.y*scale,origin.z+delta.z*scale};
    }
    return true;
}

static void ik_restore(nmo_engine_t *engine, const ik_body_t *bodies, size_t count)
{
    for (size_t i = 0; i < count; i++) eng_entity_set_local_matrix(engine,bodies[i].entity,&bodies[i].original,false);
}

bool eng_kinematic_set_position(nmo_engine_t *engine, nmo_object_id_t start,
    nmo_object_id_t end, const eng_vector_t *position)
{
    if (engine == NULL || position == NULL || !ik_finite(*position)) return false;
    int32_t first = eng_entity_index(engine,start), last = eng_entity_index(engine,end);
    if (first < 0 || last < 0) return false;
    size_t count = 0;
    int32_t current = last;
    while (current >= 0 && count < engine->entities.count) {
        if (engine->entities.data[current].destroyed ||
            nmo_engine_object_class(engine,engine->entities.data[current].id) != NMO_CID_BODYPART) return false;
        count++;
        if (current == first) break;
        current = engine->entities.data[current].parent;
    }
    if (current != first || count < 2 || count > SIZE_MAX/sizeof(ik_body_t)) return false;
    ik_body_t *bodies = calloc(count,sizeof(*bodies));
    if (bodies == NULL) return false;
    bool result = false;
    current = last;
    for (size_t i = count; i-- > 0;) {
        if (!ik_prepare_body(engine,&bodies[i],current)) goto done;
        current = engine->entities.data[current].parent;
    }
    eng_vector_t target = *position;
    float length = 0;
    for (size_t i = 1; i < count; i++) {
        eng_vector_t segment = ik_difference(eng_entity_position(engine,bodies[i].entity,-1),eng_entity_position(engine,bodies[i-1].entity,-1));
        length += eng_vector_magnitude(&segment);
    }
    if (!ik_project(eng_entity_position(engine,first,-1),&target,length,count == 2)) goto done;
    size_t unlocked = count, final = count;
    for (size_t i = 0; i+1 < count; i++) if (!bodies[i].locked) { if (unlocked == count) unlocked = i; final = i; }
    if (unlocked == count) for (size_t i = 0; i+1 < count; i++) if (bodies[i].at_limit) { if (unlocked == count) unlocked = i; final = i; }
    if (unlocked == count) goto done;
    eng_vector_t pivot = eng_entity_position(engine,bodies[unlocked].entity,-1), previous = pivot;
    length = 0;
    for (size_t i = unlocked+1; i < count; i++) if (!bodies[i].locked || i+1 == count) {
        eng_vector_t next = eng_entity_position(engine,bodies[i].entity,-1), segment = ik_difference(next,previous);
        length += eng_vector_magnitude(&segment); previous = next;
    }
    if (!ik_project(pivot,&target,length,unlocked == final)) goto done;
    eng_vector_t error = ik_difference(target,eng_entity_position(engine,last,-1));
    float initial = eng_vector_magnitude(&error), divisor = 10;
    if (initial <= .05f) { result = true; goto done; }
    ik_workspace_t workspace = {0};
    if (!ik_workspace_create(&workspace,count)) goto done;
    bool converged = false;
    result = true;
    for (int iteration = 0; iteration < 30 && !converged; iteration++) {
        error = ik_difference(target,eng_entity_position(engine,last,-1));
        eng_vector_t delta = {error.x/divisor,error.y/divisor,error.z/divisor};
        if (!ik_rotate(engine,bodies,count,&workspace,delta)) { ik_restore(engine,bodies,count); result = false; break; }
        error = ik_difference(target,eng_entity_position(engine,last,-1));
        float remaining = eng_vector_magnitude(&error), expected = eng_vector_magnitude(&delta)*divisor;
        converged = remaining <= .05f;
        if (!isfinite(remaining) || remaining >= expected) { divisor *= 2; converged = false; ik_restore(engine,bodies,count); }
        else if (2*remaining < expected) divisor *= .75f;
    }
    error = ik_difference(target,eng_entity_position(engine,last,-1));
    if (!converged && eng_vector_magnitude(&error) > initial) ik_restore(engine,bodies,count);
    free(workspace.rows); free(workspace.storage);
done:
    free(bodies);
    return result;
}

static bool ik_decompose(float **a, int m, int n, float *w, float **v, float *rv1) {
    float anorm = 0.0f;
    float scale = 0.0f;
    float g = 0.0f;

    // Householder reduction to bidiagonal form
    for (int i = 0; i < n; ++i) {
        int l = i + 1;
        rv1[i] = scale * g;
        g = 0.0f;
        float s = 0.0f;
        scale = 0.0f;

        if (i < m) {
            for (int k = i; k < m; ++k)
                scale += fabsf(a[k][i]);

            if (scale != 0.0f) {
                for (int k = i; k < m; ++k) {
                    a[k][i] /= scale;
                    s += a[k][i] * a[k][i];
                }

                float f = a[i][i];
                g = (f >= 0.0f) ? -fabsf(sqrtf(s)) : fabsf(sqrtf(s));
                float h = f * g - s;
                a[i][i] = f - g;

                for (int j = l; j < n; ++j) {
                    s = 0.0f;
                    for (int k = i; k < m; ++k)
                        s += a[k][i] * a[k][j];
                    f = s / h;
                    for (int k = i; k < m; ++k)
                        a[k][j] += f * a[k][i];
                }

                for (int k = i; k < m; ++k)
                    a[k][i] *= scale;
            }
        }

        w[i] = scale * g;
        g = 0.0f;
        s = 0.0f;
        scale = 0.0f;

        if (i < m && i != n - 1) {
            for (int k = l; k < n; ++k)
                scale += fabsf(a[i][k]);

            if (scale != 0.0f) {
                for (int k = l; k < n; ++k) {
                    a[i][k] /= scale;
                    s += a[i][k] * a[i][k];
                }

                float f = a[i][l];
                g = (f >= 0.0f) ? -fabsf(sqrtf(s)) : fabsf(sqrtf(s));
                float h = f * g - s;
                a[i][l] = f - g;

                for (int k = l; k < n; ++k)
                    rv1[k] = a[i][k] / h;

                for (int j = l; j < m; ++j) {
                    s = 0.0f;
                    for (int k = l; k < n; ++k)
                        s += a[j][k] * a[i][k];
                    for (int k = l; k < n; ++k)
                        a[j][k] += s * rv1[k];
                }

                for (int k = l; k < n; ++k)
                    a[i][k] *= scale;
            }
        }

        float tmp = fabsf(w[i]) + fabsf(rv1[i]);
        if (tmp > anorm)
            anorm = tmp;
    }

    // Accumulation of right-hand transformations
    for (int i = n - 1; i >= 0; --i) {
        int l = i + 1;
        if (i < n - 1) {
            if (g != 0.0f) {
                for (int j = l; j < n; ++j)
                    v[j][i] = (a[i][j] / a[i][l]) / g;

                for (int j = l; j < n; ++j) {
                    float s = 0.0f;
                    for (int k = l; k < n; ++k)
                        s += a[i][k] * v[k][j];
                    for (int k = l; k < n; ++k)
                        v[k][j] += s * v[k][i];
                }
            }

            for (int j = l; j < n; ++j) {
                v[j][i] = 0.0f;
                v[i][j] = 0.0f;
            }
        }

        v[i][i] = 1.0f;
        g = rv1[i];
    }

    // Accumulation of left-hand transformations
    int minmn = (m < n) ? m : n;
    for (int i = minmn - 1; i >= 0; --i) {
        int l = i + 1;
        g = w[i];

        for (int j = l; j < n; ++j)
            a[i][j] = 0.0f;

        if (g == 0.0f) {
            for (int j = i; j < m; ++j)
                a[j][i] = 0.0f;
        } else {
            g = 1.0f / g;
            for (int j = l; j < n; ++j) {
                float s = 0.0f;
                for (int k = l; k < m; ++k)
                    s += a[k][i] * a[k][j];
                float f = (s / a[i][i]) * g;
                for (int k = i; k < m; ++k)
                    a[k][j] += f * a[k][i];
            }

            for (int j = i; j < m; ++j)
                a[j][i] *= g;
        }

        a[i][i] += 1.0f;
    }

    // Diagonalization of the bidiagonal form
    for (int k = n - 1; k >= 0; --k) {
        for (int its = 1; its <= 30; ++its) {
            int flag = 1;
            int l, nm;

            for (l = k; l >= 0; --l) {
                nm = l - 1;
                if (fabsf(rv1[l]) + anorm == anorm) {
                    flag = 0;
                    break;
                }
                if (nm >= 0 && fabsf(w[nm]) + anorm == anorm)
                    break;
            }

            if (flag) {
                float c = 0.0f;
                float s = 1.0f;
                for (int i = l; i <= k; ++i) {
                    float f = s * rv1[i];
                    rv1[i] = c * rv1[i];
                    if (fabsf(f) + anorm == anorm)
                        break;

                    g = w[i];
                    float h = (float)sqrt((double)f * f + (double)g * g);
                    w[i] = h;
                    h = 1.0f / h;
                    c = g * h;
                    s = -f * h;

                    for (int j = 0; j < m; ++j) {
                        float y = a[j][nm];
                        float z = a[j][i];
                        a[j][nm] = y * c + z * s;
                        a[j][i] = z * c - y * s;
                    }
                }
            }

            float z = w[k];
            if (l == k) {
                if (z < 0.0f) {
                    w[k] = -z;
                    for (int j = 0; j < n; ++j)
                        v[j][k] = -v[j][k];
                }
                break;
            }

            if (its == 30) {
                return false;
            }

            float x = w[l];
            nm = k - 1;
            float y = (nm >= 0) ? w[nm] : 0.0f;
            g = (nm >= 0) ? rv1[nm] : 0.0f;
            float h = rv1[k];
            float f = (float)(((double)(y - z) * ((double)y + z) + (double)(g - h) * ((double)g + h)) / (2.0 * h * y));
            g = (float)sqrt((double)f * f + 1.0);
            float sign_g = (f >= 0.0f) ? fabsf(g) : -fabsf(g);
            f = (float)(((double)(x - z) * ((double)x + z) + (double)h * ((y / (f + sign_g)) - h)) / x);

            float c = 1.0f;
            float s = 1.0f;

            for (int j = l; j <= nm; ++j) {
                int i = j + 1;
                g = rv1[i];
                y = w[i];
                h = s * g;
                g = c * g;
                z = (float)sqrt((double)f * f + (double)h * h);
                rv1[j] = z;
                c = f / z;
                s = h / z;
                f = x * c + g * s;
                g = g * c - x * s;
                h = y * s;
                y = y * c;

                for (int jj = 0; jj < n; ++jj) {
                    x = v[jj][j];
                    z = v[jj][i];
                    v[jj][j] = x * c + z * s;
                    v[jj][i] = z * c - x * s;
                }

                z = (float)sqrt((double)f * f + (double)h * h);
                w[j] = z;
                if (z != 0.0f) {
                    z = 1.0f / z;
                    c = f * z;
                    s = h * z;
                }

                f = c * g + s * y;
                x = c * y - s * g;

                for (int jj = 0; jj < m; ++jj) {
                    y = a[jj][j];
                    z = a[jj][i];
                    a[jj][j] = y * c + z * s;
                    a[jj][i] = z * c - y * s;
                }
            }

            rv1[l] = 0.0f;
            rv1[k] = f;
            w[k] = x;
        }
    }

    return true;
}
