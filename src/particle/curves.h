/**
 * @file particle/curves.h
 * @brief Checks of the keyed curves of ParticleCurves and ParticleLight, shared by the validate
 * hooks, which log the problem, and the extract, which draws a bad curve as its default.
 */

#ifndef BOX_PARTICLE_CURVES_H
#define BOX_PARTICLE_CURVES_H

#include <box.h>

/** Check a ParticleCurve: its keys and its max_keys are each a valid Curve of its kind whose
 * times are from 0 to 1.
 *
 * @param curve The curve
 * @return NULL when it is valid, else the first problem naming the list and the key, for
 *         example "key 2: time 1.5 is outside of 0 to 1" or "max_keys key 0: value is not
 *         finite", to free with ecs_os_free
 */
char* box_particle_curve_check(
    const ParticleCurve *curve);

/** Check the curve of a ParticleLight: a valid ParticleCurve without max_keys, as a light has
 * no random draw to blend two curves by.
 *
 * @param curve The curve
 * @return NULL when it is valid, else the first problem, to free with ecs_os_free
 */
char* box_particle_light_curve_check(
    const ParticleCurve *curve);

/** Check a ParticleGradient: the kind is in range and the times of its keys and of its
 * max_keys are finite, increase strictly and are from 0 to 1.
 *
 * @param gradient The gradient
 * @return NULL when it is valid, else the first problem naming the key, to free with
 *         ecs_os_free
 */
char* box_particle_gradient_check(
    const ParticleGradient *gradient);

/** Check whether a ParticleCurve is valid, without a message.
 *
 * @param curve The curve
 * @return Whether box_particle_curve_check finds no problem
 */
bool box_particle_curve_valid(
    const ParticleCurve *curve);

/** Check whether the curve of a ParticleLight is valid, without a message.
 *
 * @param curve The curve
 * @return Whether box_particle_light_curve_check finds no problem
 */
bool box_particle_light_curve_valid(
    const ParticleCurve *curve);

/** Check whether a ParticleGradient is valid, without a message.
 *
 * @param gradient The gradient
 * @return Whether box_particle_gradient_check finds no problem
 */
bool box_particle_gradient_valid(
    const ParticleGradient *gradient);

/** Install the validate hooks of ParticleCurves, ParticleMotion and ParticleLight, which log the
 * first problem of every bad curve with the entity, the component and the field.
 *
 * @param world The world
 */
void box_particle_curves_import(
    ecs_world_t *world);

#endif
