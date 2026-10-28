#ifndef FLECS_ENGINE_SURFACE_H
#define FLECS_ENGINE_SURFACE_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_SURFACE_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(FlecsAnisotropy, {
    FlecsAnisotropyDefault = 0,
    FlecsAnisotropyOff     = 1,
    FlecsAnisotropyLow     = 2,
    FlecsAnisotropyMedium  = 4,
    FlecsAnisotropyHigh    = 8,
    FlecsAnisotropyUltra   = 16,
});

ECS_ENUM(FlecsMsaa, {
    FlecsMsaaDefault = 0,
    FlecsMsaaOff     = 1,
    FlecsMsaa2x      = 2,
    FlecsMsaa4x      = 4,
    FlecsMsaa8x      = 8,
});

ECS_ENUM(FlecsTextureQuality, {
    FlecsTextureQualityDefault  = 0,
    FlecsTextureQualityLow      = 512,
    FlecsTextureQualityMedium   = 1024,
    FlecsTextureQualityHigh     = 2048,
    FlecsTextureQualityVeryHigh = 4096,
});

ECS_ENUM(FlecsTextureCompression, {
    FlecsTextureCompressionDefault = 0,
    FlecsTextureCompressionOff     = 1,
    FlecsTextureCompressionBc7     = 2,
});

ECS_ENUM(FlecsTextureCacheMode, {
    FlecsTextureCacheDefault = 0,
    FlecsTextureCacheOff     = 1,
    FlecsTextureCacheOn      = 2,
});

ECS_STRUCT(FlecsSurface, {
    int32_t width;
    int32_t height;
    int32_t resolution_scale;
    int32_t actual_width;
    int32_t actual_height;
    int32_t point_width;
    int32_t point_height;
    FlecsMsaa msaa;
    bool vsync;
    float target_fps;
    int32_t frames_in_flight;
    int32_t load_submits_in_flight;
    bool gpu_timings;
    bool mem_attribution;
    bool keep_source_textures;
    /* Hide the cursor and lock it to the window so mouse.rel reports raw
     * motion (first person look). While captured mouse.view/view_norm read as
     * the center of the view, so picking rays go through a crosshair. */
    bool cursor_captured;
    FlecsAnisotropy anisotropy;
    FlecsTextureQuality texture_quality;
    FlecsTextureCompression texture_compression;
    FlecsTextureCacheMode texture_cache;
    const char *texture_cache_path;
    const char *title;
    const char *write_to_file;
    int32_t write_nth;
});

#endif
