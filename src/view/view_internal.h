/**
 * @file view/view_internal.h
 * @brief Private components of the box.view module.
 */

#ifndef BOX_VIEW_INTERNAL_H
#define BOX_VIEW_INTERNAL_H

#include <box.h>

/** Where a view ended up in the frame packet */
typedef struct ViewImpl {
    uint32_t index;  /**< Index of the view in the packet view array */
    bool extracted;  /**< The view produced an entry in the last packet */
    bool warned;     /**< The missing camera of the view was already reported */
} ViewImpl;

/** Render target handle of a RenderTarget entity and the size it was created for */
typedef struct RenderTargetImpl {
    uint32_t handle; /**< Target handle the packet names, 0 before the first extract */
    int32_t width;   /**< Width the handle was created for */
    int32_t height;  /**< Height the handle was created for */
} RenderTargetImpl;

extern ECS_COMPONENT_DECLARE(ViewImpl);
extern ECS_COMPONENT_DECLARE(RenderTargetImpl);

#endif
