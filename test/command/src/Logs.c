/**
 * @file Logs.c
 * @brief Tests for the captured log ring, log.txt and the logs command.
 */

#include <box.h>
#include <bake_test.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;
static void *log_out;

void Logs_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);
    log_out = ecs_os_api.log_out_;
    ecs_os_api.log_out_ = fopen("/dev/null", "w");
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Logs_teardown(void)
{
    FILE *devnull = ecs_os_api.log_out_;
    ecs_os_api.log_out_ = log_out;
    if (devnull) {
        fclose(devnull);
    }

    ecs_fini(w);
}

void Logs_filter_by_level(void)
{
    // command/logs
    int prev = ecs_log_set_level(1);
    ecs_err("logtest err");
    ecs_warn("logtest warn");
    ecs_trace("logtest info");
    ecs_dbg("logtest debug");
    ecs_log_set_level(prev);

    char *out = box_command_run(w, session, "logs error 100", NULL);
    test_assert(strstr(out, "error\tlogtest err\n") != NULL);
    test_assert(strstr(out, "logtest warn") == NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "logs warning 100", NULL);
    test_assert(strstr(out, "error\tlogtest err\n") != NULL);
    test_assert(strstr(out, "warning\tlogtest warn\n") != NULL);
    test_assert(strstr(out, "logtest info") == NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "logs info 100", NULL);
    test_assert(strstr(out, "warning\tlogtest warn\n") != NULL);
    test_assert(strstr(out, "info\tlogtest info\n") != NULL);
    test_assert(strstr(out, "logtest debug") == NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "logs debug 100", NULL);
    test_assert(strstr(out, "info\tlogtest info\n") != NULL);
    test_assert(strstr(out, "debug\tlogtest debug\n") != NULL);
    ecs_os_free(out);
}

void Logs_numeric_level(void)
{
    // command/logs
    ecs_err("numtest err");
    ecs_warn("numtest warn");

    char *out = box_command_run(w, session, "logs -3 100", NULL);
    test_assert(strstr(out, "error\tnumtest err\n") != NULL);
    test_assert(strstr(out, "numtest warn") == NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "logs -2 100", NULL);
    test_assert(strstr(out, "warning\tnumtest warn\n") != NULL);
    ecs_os_free(out);
}

void Logs_oldest_first(void)
{
    // command/logs
    ecs_warn("o1");
    ecs_warn("o2");
    ecs_warn("o3");

    char *out = box_command_run(w, session, "logs warning 3", NULL);
    test_str(out, "warning\to1\nwarning\to2\nwarning\to3\n");
    ecs_os_free(out);
}

void Logs_count_cap(void)
{
    // command/logs
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_warn("c%d", i);
    }

    char *out = box_command_run(w, session, "logs warning 3", NULL);
    test_str(out, "warning\tc7\nwarning\tc8\nwarning\tc9\n");
    ecs_os_free(out);
}

void Logs_wrap_around(void)
{
    // command/logs
    int32_t i;
    for (i = 0; i < 1100; i ++) {
        ecs_warn("w%d", i);
    }

    char *out = box_command_run(w, session, "logs warning 1024", NULL);
    test_assert(strstr(out, "warning\tw76\n") == out);
    test_assert(strstr(out, "warning\tw1099\n") != NULL);
    test_assert(strstr(out, "\tw75\n") == NULL);
    test_assert(strstr(out, "\tw0\n") == NULL);

    int32_t lines = 0;
    const char *p = out;
    while (p[0]) {
        if (p[0] == '\n') {
            lines ++;
        }

        p ++;
    }

    test_int(lines, 1024);
    ecs_os_free(out);
}

void Logs_log_file_in_process_dir(void)
{
    // command/logs
    ecs_warn("filetest warn");
    char *path = flecs_asprintf(
        "%s/log.txt", ecs_singleton_get(w, App)->process_dir);
    struct stat st;
    test_int(stat(path, &st), 0);
    test_assert(S_ISREG(st.st_mode));

    FILE *f = fopen(path, "r");
    test_assert(f != NULL);
    char buffer[4096];
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);
    test_assert(size > 0);
    buffer[size] = '\0';
    test_assert(strstr(buffer, "warning\tfiletest warn\n") != NULL);
    ecs_os_free(path);
}

void Logs_default_arguments(void)
{
    // command/logs
    int prev = ecs_log_set_level(1);
    ecs_trace("deftest info");
    ecs_log_set_level(prev);

    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_warn("d%d", i);
    }

    char *out = box_command_run(w, session, "logs", NULL);
    test_assert(strstr(out, "deftest info") == NULL);
    test_assert(strstr(out, "warning\td10\n") == out);
    test_assert(strstr(out, "warning\td59\n") != NULL);
    test_assert(strstr(out, "\td9\n") == NULL);

    int32_t lines = 0;
    const char *p = out;
    while (p[0]) {
        if (p[0] == '\n') {
            lines ++;
        }

        p ++;
    }

    test_int(lines, 50);
    ecs_os_free(out);
}

void Logs_invalid_level_name(void)
{
    // command/logs
    char *out = box_command_run(w, session, "logs bogus", NULL);
    test_str(out, "unknown log level 'bogus'");
    ecs_os_free(out);

    out = box_command_run(w, session, "logs 3x 10", NULL);
    test_str(out, "unknown log level '3x'");
    ecs_os_free(out);
}
