
/* A friendly warning from bake.test
 * ----------------------------------------------------------------------------
 * This file is generated. To add/remove testcases modify the 'project.json' of
 * the test project. ANY CHANGE TO THIS FILE IS LOST AFTER (RE)BUILDING!
 * ----------------------------------------------------------------------------
 */

#include <bake_test.h>

// Testsuite 'Flat'
void Flat_quad(void);
void Flat_triangle(void);
void Flat_right_triangle(void);
void Flat_ngon_matches_triangle_at_three_sides(void);
void Flat_ngon_clamps_minimum(void);
void Flat_ngon(void);

// Testsuite 'Box'
void Box_box(void);
void Box_rounded_box(void);
void Box_rounded_box_clamps_radius(void);
void Box_rounded_box_clamps_segments(void);
void Box_triangle_prism(void);
void Box_right_triangle_prism(void);

// Testsuite 'Round'
void Round_sphere_smooth(void);
void Round_sphere_flat(void);
void Round_hemisphere_smooth(void);
void Round_hemisphere_flat(void);
void Round_icosphere_base_smooth(void);
void Round_icosphere_base_flat(void);
void Round_icosphere_subdivided(void);

// Testsuite 'Axis'
void Axis_cone_smooth(void);
void Axis_cone_flat(void);
void Axis_cylinder_smooth(void);
void Axis_cylinder_flat(void);
void Axis_frustum_smooth(void);
void Axis_frustum_flat(void);
void Axis_capsule_smooth(void);
void Axis_capsule_flat(void);
void Axis_tube_smooth(void);
void Axis_tube_flat(void);
void Axis_tube_open_arc(void);
void Axis_torus_smooth(void);
void Axis_torus_flat(void);

bake_test_case Flat_testcases[] = {
    {
        "quad",
        Flat_quad
    },
    {
        "triangle",
        Flat_triangle
    },
    {
        "right_triangle",
        Flat_right_triangle
    },
    {
        "ngon_matches_triangle_at_three_sides",
        Flat_ngon_matches_triangle_at_three_sides
    },
    {
        "ngon_clamps_minimum",
        Flat_ngon_clamps_minimum
    },
    {
        "ngon",
        Flat_ngon
    }
};

bake_test_case Box_testcases[] = {
    {
        "box",
        Box_box
    },
    {
        "rounded_box",
        Box_rounded_box
    },
    {
        "rounded_box_clamps_radius",
        Box_rounded_box_clamps_radius
    },
    {
        "rounded_box_clamps_segments",
        Box_rounded_box_clamps_segments
    },
    {
        "triangle_prism",
        Box_triangle_prism
    },
    {
        "right_triangle_prism",
        Box_right_triangle_prism
    }
};

bake_test_case Round_testcases[] = {
    {
        "sphere_smooth",
        Round_sphere_smooth
    },
    {
        "sphere_flat",
        Round_sphere_flat
    },
    {
        "hemisphere_smooth",
        Round_hemisphere_smooth
    },
    {
        "hemisphere_flat",
        Round_hemisphere_flat
    },
    {
        "icosphere_base_smooth",
        Round_icosphere_base_smooth
    },
    {
        "icosphere_base_flat",
        Round_icosphere_base_flat
    },
    {
        "icosphere_subdivided",
        Round_icosphere_subdivided
    }
};

bake_test_case Axis_testcases[] = {
    {
        "cone_smooth",
        Axis_cone_smooth
    },
    {
        "cone_flat",
        Axis_cone_flat
    },
    {
        "cylinder_smooth",
        Axis_cylinder_smooth
    },
    {
        "cylinder_flat",
        Axis_cylinder_flat
    },
    {
        "frustum_smooth",
        Axis_frustum_smooth
    },
    {
        "frustum_flat",
        Axis_frustum_flat
    },
    {
        "capsule_smooth",
        Axis_capsule_smooth
    },
    {
        "capsule_flat",
        Axis_capsule_flat
    },
    {
        "tube_smooth",
        Axis_tube_smooth
    },
    {
        "tube_flat",
        Axis_tube_flat
    },
    {
        "tube_open_arc",
        Axis_tube_open_arc
    },
    {
        "torus_smooth",
        Axis_torus_smooth
    },
    {
        "torus_flat",
        Axis_torus_flat
    }
};

static bake_test_suite suites[] = {
    {
        "Flat",
        NULL,
        NULL,
        6,
        Flat_testcases
    },
    {
        "Box",
        NULL,
        NULL,
        6,
        Box_testcases
    },
    {
        "Round",
        NULL,
        NULL,
        7,
        Round_testcases
    },
    {
        "Axis",
        NULL,
        NULL,
        13,
        Axis_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("geometry", argc, argv, suites, 4);
}
