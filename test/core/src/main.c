
/* A friendly warning from bake.test
 * ----------------------------------------------------------------------------
 * This file is generated. To add/remove testcases modify the 'project.json' of
 * the test project. ANY CHANGE TO THIS FILE IS LOST AFTER (RE)BUILDING!
 * ----------------------------------------------------------------------------
 */

#include <bake_test.h>

// Testsuite 'Args'
void Args_no_args_defaults(void);
void Args_headless_requires_stop_condition(void);
void Args_headless_with_frames(void);
void Args_timeout_seconds(void);
void Args_timeout_infinite(void);
void Args_cpu_budget(void);
void Args_fps(void);
void Args_unknown_option_fails(void);
void Args_scene_file_loads(void);
void Args_missing_scene_fails(void);

// Testsuite 'App'
void App_stops_after_frames(void);
void App_stops_after_timeout(void);
void App_stops_on_quit(void);
void App_process_dir_created_and_removed(void);
void App_cpu_budget_slows_loop(void);

// Testsuite 'Queue'
void Queue_init_all_free(void);
void Queue_acquire_submit_wait_release(void);
void Queue_poll_returns_null_when_empty(void);
void Queue_fifo_order(void);
void Queue_acquire_returns_null_after_stop(void);
void Queue_acquire_blocks_until_release(void);
void Queue_wait_blocks_until_submit(void);

// Testsuite 'Surface'
void Surface_headless_surface_reports_size(void);
void Surface_headless_surface_writes_png(void);
void Surface_write_nth_adds_frame_suffix(void);

bake_test_case Args_testcases[] = {
    {
        "no_args_defaults",
        Args_no_args_defaults
    },
    {
        "headless_requires_stop_condition",
        Args_headless_requires_stop_condition
    },
    {
        "headless_with_frames",
        Args_headless_with_frames
    },
    {
        "timeout_seconds",
        Args_timeout_seconds
    },
    {
        "timeout_infinite",
        Args_timeout_infinite
    },
    {
        "cpu_budget",
        Args_cpu_budget
    },
    {
        "fps",
        Args_fps
    },
    {
        "unknown_option_fails",
        Args_unknown_option_fails
    },
    {
        "scene_file_loads",
        Args_scene_file_loads
    },
    {
        "missing_scene_fails",
        Args_missing_scene_fails
    }
};

bake_test_case App_testcases[] = {
    {
        "stops_after_frames",
        App_stops_after_frames
    },
    {
        "stops_after_timeout",
        App_stops_after_timeout
    },
    {
        "stops_on_quit",
        App_stops_on_quit
    },
    {
        "process_dir_created_and_removed",
        App_process_dir_created_and_removed
    },
    {
        "cpu_budget_slows_loop",
        App_cpu_budget_slows_loop
    }
};

bake_test_case Queue_testcases[] = {
    {
        "init_all_free",
        Queue_init_all_free
    },
    {
        "acquire_submit_wait_release",
        Queue_acquire_submit_wait_release
    },
    {
        "poll_returns_null_when_empty",
        Queue_poll_returns_null_when_empty
    },
    {
        "fifo_order",
        Queue_fifo_order
    },
    {
        "acquire_returns_null_after_stop",
        Queue_acquire_returns_null_after_stop
    },
    {
        "acquire_blocks_until_release",
        Queue_acquire_blocks_until_release
    },
    {
        "wait_blocks_until_submit",
        Queue_wait_blocks_until_submit
    }
};

bake_test_case Surface_testcases[] = {
    {
        "headless_surface_reports_size",
        Surface_headless_surface_reports_size
    },
    {
        "headless_surface_writes_png",
        Surface_headless_surface_writes_png
    },
    {
        "write_nth_adds_frame_suffix",
        Surface_write_nth_adds_frame_suffix
    }
};

static bake_test_suite suites[] = {
    {
        "Args",
        NULL,
        NULL,
        10,
        Args_testcases
    },
    {
        "App",
        NULL,
        NULL,
        5,
        App_testcases
    },
    {
        "Queue",
        NULL,
        NULL,
        7,
        Queue_testcases
    },
    {
        "Surface",
        NULL,
        NULL,
        3,
        Surface_testcases
    }
};

int main(int argc, char *argv[]) {
    return bake_test_run("core", argc, argv, suites, 4);
}
