/**
 * @file transform/transform_version.h
 * @brief Private component of box.transform that records when a static transform was computed.
 */

#ifndef BOX_TRANSFORM_VERSION_H
#define BOX_TRANSFORM_VERSION_H

#include <box.h>

#ifdef BOX_TRANSFORM_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** WorldTransform version, used to deduplicate static transforms. */
ECS_STRUCT(WorldTransformVersion, {
    uint64_t version; /**< Frame the static transform was last computed in */
});

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
