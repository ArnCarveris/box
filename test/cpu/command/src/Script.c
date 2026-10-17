/**
 * @file Script.c
 * @brief Tests for the script_read, script_write, script_grep and script_patch commands.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void Script_read_script(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_read_script.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_read_script.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "script_read Script_read_script.flecs", &error);
    test_bool(error, false);
    test_str(out, "Foo {}\n");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read ./Script_read_script.flecs", &error);
    test_bool(error, false);
    test_str(out, "Foo {}\n");
    ecs_os_free(out);

    remove("Script_read_script.flecs");
    ecs_fini(world);
}

void Script_read_absolute_path(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_read_absolute_path.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_read_absolute_path.flecs" }) != 0);

    char cwd[1024];
    test_assert(getcwd(cwd, sizeof(cwd)) != NULL);
    char *line = flecs_asprintf("script_read %s/Script_read_absolute_path.flecs", cwd);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, line, &error);
    test_bool(error, false);
    test_str(out, "Foo {}\n");
    ecs_os_free(out);

    ecs_os_free(line);
    remove("Script_read_absolute_path.flecs");
    ecs_fini(world);
}

void Script_read_lists_scripts(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_read_lists_a.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\n", f);
    fclose(f);
    f = fopen("Script_read_lists_b.flecs", "w");
    test_assert(f != NULL);
    fputs("B {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_read_lists_a.flecs" }) != 0);
    test_assert(ecs_script(world, { .filename = "Script_read_lists_b.flecs" }) != 0);
    test_assert(ecs_script(world, { .code = "C {}" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "script_read", &error);
    test_bool(error, false);
    test_str(out, "Script_read_lists_a.flecs\nScript_read_lists_b.flecs");
    ecs_os_free(out);

    remove("Script_read_lists_a.flecs");
    remove("Script_read_lists_b.flecs");
    ecs_fini(world);
}

void Script_read_unknown_script(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "script_read nope.flecs", &error);
    test_bool(error, true);
    test_str(out, "error: no script 'nope.flecs'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Script_read_outside_working_dir(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/Script_read_outside_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("Foo {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = path }) != 0);

    char *line = flecs_asprintf("script_read %s", path);
    char *expect = flecs_asprintf("error: '%s' is outside the working directory", path);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, line, &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read /etc/hosts", &error);
    test_bool(error, true);
    test_str(out, "error: '/etc/hosts' is outside the working directory");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    remove(path);
    ecs_os_free(expect);
    ecs_os_free(line);
    ecs_os_free(path);
    ecs_fini(world);
}

void Script_read_parent_path(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "script_read ../x.flecs", &error);
    test_bool(error, true);
    test_str(out, "error: '../x.flecs' is outside the working directory");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read a/../../x.flecs", &error);
    test_bool(error, true);
    test_str(out, "error: 'a/../../x.flecs' is outside the working directory");
    ecs_os_free(out);

    ecs_fini(world);
}

void Script_read_symlink_outside(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/Script_read_symlink_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("Foo {}\n", f);
    fclose(f);
    remove("Script_read_symlink_outside.flecs");
    test_int(symlink(path, "Script_read_symlink_outside.flecs"), 0);
    test_assert(ecs_script(world, { .filename = "Script_read_symlink_outside.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "script_read Script_read_symlink_outside.flecs", &error);
    test_bool(error, true);
    test_str(out,
        "error: 'Script_read_symlink_outside.flecs' is outside the working directory");
    ecs_os_free(out);

    out = box_terminal_run(world, s,
        "script_write Script_read_symlink_outside.flecs Bar {}", &error);
    test_bool(error, true);
    ecs_os_free(out);

    char buf[64] = {0};
    f = fopen(path, "r");
    test_assert(f != NULL);
    test_assert(fread(buf, 1, sizeof(buf) - 1, f) > 0);
    fclose(f);
    test_str(buf, "Foo {}\n");

    remove("Script_read_symlink_outside.flecs");
    remove(path);
    ecs_os_free(path);
    ecs_fini(world);
}

void Script_write_updates_world(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_write_updates_world.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo { box.transform.Position: {1, 2, 3} }\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_write_updates_world.flecs" }) != 0);
    test_assert(ecs_lookup(world, "Foo") != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_write Script_write_updates_world.flecs "
        "\"Bar { box.transform.Position: {4, 5, 6} }\"",
        &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "Foo") == 0);
    ecs_entity_t bar = ecs_lookup(world, "Bar");
    test_assert(bar != 0);
    const Position *p = ecs_get(world, bar, Position);
    test_assert(p != NULL);
    test_flt(p->x, 4);
    test_flt(p->y, 5);
    test_flt(p->z, 6);

    out = box_terminal_run(world, s, "script_read Script_write_updates_world.flecs", &error);
    test_str(out, "Bar { box.transform.Position: {4, 5, 6} }");
    ecs_os_free(out);

    remove("Script_write_updates_world.flecs");
    ecs_fini(world);
}

void Script_write_writes_file(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_write_writes_file.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_write_writes_file.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_write Script_write_writes_file.flecs Bar {}\nBaz {}", &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "Bar") != 0);
    test_assert(ecs_lookup(world, "Baz") != 0);

    char buf[64] = {0};
    f = fopen("Script_write_writes_file.flecs", "r");
    test_assert(f != NULL);
    test_assert(fread(buf, 1, sizeof(buf) - 1, f) > 0);
    fclose(f);
    test_str(buf, "Bar {}\nBaz {}");

    remove("Script_write_writes_file.flecs");
    ecs_fini(world);
}

void Script_write_parse_error(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_write_parse_error.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_write_parse_error.flecs" }) != 0);

    ecs_log_set_level(-4);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "script_write Script_write_parse_error.flecs Bar {", &error);
    test_bool(error, true);
    test_assert(!strncmp(out, "error: ", 7));
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "Foo") != 0);
    test_assert(ecs_lookup(world, "Bar") == 0);

    out = box_terminal_run(world, s, "script_read Script_write_parse_error.flecs", &error);
    test_bool(error, false);
    test_str(out, "Foo {}\n");
    ecs_os_free(out);

    char buf[64] = {0};
    f = fopen("Script_write_parse_error.flecs", "r");
    test_assert(f != NULL);
    test_assert(fread(buf, 1, sizeof(buf) - 1, f) > 0);
    fclose(f);
    test_str(buf, "Foo {}\n");

    remove("Script_write_parse_error.flecs");
    ecs_fini(world);
}

void Script_write_eval_error(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_write_eval_error.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo { box.transform.Position: {1, 2, 3} }\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_write_eval_error.flecs" }) != 0);

    ecs_log_set_level(-4);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "script_write Script_write_eval_error.flecs Bar { NoSuchComponent }", &error);
    test_bool(error, true);
    test_assert(strstr(out, "NoSuchComponent") != NULL);
    ecs_os_free(out);

    ecs_entity_t foo = ecs_lookup(world, "Foo");
    test_assert(foo != 0);
    test_assert(ecs_has(world, foo, Position));
    test_assert(ecs_lookup(world, "Bar") == 0);

    out = box_terminal_run(world, s, "script_read Script_write_eval_error.flecs", &error);
    test_bool(error, false);
    test_str(out, "Foo { box.transform.Position: {1, 2, 3} }\n");
    ecs_os_free(out);

    remove("Script_write_eval_error.flecs");
    ecs_fini(world);
}

void Script_write_unknown_script(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "script_write nope.flecs Foo {}", &error);
    test_bool(error, true);
    test_str(out, "error: no script 'nope.flecs'");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "Foo") == 0);
    test_assert(access("nope.flecs", F_OK) != 0);

    ecs_fini(world);
}

void Script_write_missing_code(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "script_write nope.flecs", &error);
    test_bool(error, true);
    test_str(out, "error: script_write takes 2 arguments, got 1");
    ecs_os_free(out);

    ecs_fini(world);
}

void Script_grep_matches(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_grep_matches.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo {\n  box.transform.Position: {1, 2, 3}\n}\n"
        "Bar {\n  box.transform.Position: {4, 5, 6}\n}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_grep_matches.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "script_grep Position", &error);
    test_bool(error, false);
    test_str(out, "Script_grep_matches.flecs:2:  box.transform.Position: {1, 2, 3}\n"
        "Script_grep_matches.flecs:5:  box.transform.Position: {4, 5, 6}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_grep {4, 5", &error);
    test_bool(error, false);
    test_str(out, "Script_grep_matches.flecs:5:  box.transform.Position: {4, 5, 6}");
    ecs_os_free(out);

    remove("Script_grep_matches.flecs");
    ecs_fini(world);
}

void Script_grep_multiple_scripts(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_grep_multiple_a.flecs", "w");
    test_assert(f != NULL);
    fputs("Apple {}\n", f);
    fclose(f);
    f = fopen("Script_grep_multiple_b.flecs", "w");
    test_assert(f != NULL);
    fputs("Pear {}\nPineapple {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_grep_multiple_a.flecs" }) != 0);
    test_assert(ecs_script(world, { .filename = "Script_grep_multiple_b.flecs" }) != 0);
    test_assert(ecs_script(world, { .code = "Grapple {}" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "script_grep pple", &error);
    test_bool(error, false);
    test_str(out, "Script_grep_multiple_a.flecs:1:Apple {}\n"
        "Script_grep_multiple_b.flecs:2:Pineapple {}");
    ecs_os_free(out);

    remove("Script_grep_multiple_a.flecs");
    remove("Script_grep_multiple_b.flecs");
    ecs_fini(world);
}

void Script_grep_no_match(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_grep_no_match.flecs", "w");
    test_assert(f != NULL);
    fputs("Foo {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_grep_no_match.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "script_grep Bar", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    remove("Script_grep_no_match.flecs");
    ecs_fini(world);
}

void Script_grep_skips_outside(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = flecs_asprintf("/tmp/Script_grep_skips_%d.flecs", (int)getpid());
    FILE *f = fopen(path, "w");
    test_assert(f != NULL);
    fputs("Secret {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = path }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "script_grep Secret", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    remove(path);
    ecs_os_free(path);
    ecs_fini(world);
}

void Script_grep_limit(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_grep_limit.flecs", "w");
    test_assert(f != NULL);
    int i;
    for (i = 0; i < 105; i ++) {
        fprintf(f, "E%d {}\n", i);
    }

    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_grep_limit.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "script_grep {}", &error);
    test_bool(error, false);
    test_assert(strstr(out, "Script_grep_limit.flecs:100:E99 {}\n... 5 more matches") != NULL);
    test_assert(strstr(out, "E100") == NULL);
    ecs_os_free(out);

    remove("Script_grep_limit.flecs");
    ecs_fini(world);
}

void Script_patch_replace_line(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_replace_line.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\nC {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_replace_line.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_replace_line.flecs "
        "--- a/Script_patch_replace_line.flecs\n"
        "+++ b/Script_patch_replace_line.flecs\n"
        "@@ -1,3 +1,3 @@\n"
        " A {}\n"
        "-B {}\n"
        "+D {}\n"
        " C {}\n", &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "A") != 0);
    test_assert(ecs_lookup(world, "B") == 0);
    test_assert(ecs_lookup(world, "C") != 0);
    test_assert(ecs_lookup(world, "D") != 0);

    out = box_terminal_run(world, s, "script_read Script_patch_replace_line.flecs", &error);
    test_str(out, "A {}\nD {}\nC {}\n");
    ecs_os_free(out);

    remove("Script_patch_replace_line.flecs");
    ecs_fini(world);
}

void Script_patch_add_lines(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_add_lines.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_add_lines.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_add_lines.flecs "
        "@@ -1,2 +1,4 @@\n"
        " A {}\n"
        "+X {}\n"
        " B {}\n"
        "+Y {}\n", &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_add_lines.flecs", &error);
    test_str(out, "A {}\nX {}\nB {}\nY {}\n");
    ecs_os_free(out);

    out = box_terminal_run(world, s,
        "script_patch Script_patch_add_lines.flecs "
        "@@ -0,0 +1 @@\n"
        "+First {}\n", &error);
    test_bool(error, false);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_add_lines.flecs", &error);
    test_str(out, "First {}\nA {}\nX {}\nB {}\nY {}\n");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "First") != 0);
    test_assert(ecs_lookup(world, "Y") != 0);

    remove("Script_patch_add_lines.flecs");
    ecs_fini(world);
}

void Script_patch_remove_lines(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_remove_lines.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\nC {}\nD {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_remove_lines.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_remove_lines.flecs "
        "@@ -2,2 +1,0 @@\n"
        "-B {}\n"
        "-C {}\n", &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_remove_lines.flecs", &error);
    test_str(out, "A {}\nD {}\n");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "B") == 0);
    test_assert(ecs_lookup(world, "C") == 0);
    test_assert(ecs_lookup(world, "D") != 0);

    remove("Script_patch_remove_lines.flecs");
    ecs_fini(world);
}

void Script_patch_multiple_hunks(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_multiple_hunks.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\nC {}\nD {}\nE {}\nF {}\nG {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_multiple_hunks.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_multiple_hunks.flecs "
        "@@ -1,2 +1,2 @@\n"
        "-A {}\n"
        "+A2 {}\n"
        " B {}\n"
        "@@ -6,2 +6,3 @@\n"
        " F {}\n"
        "+F2 {}\n"
        " G {}\n", &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_multiple_hunks.flecs", &error);
    test_str(out, "A2 {}\nB {}\nC {}\nD {}\nE {}\nF {}\nF2 {}\nG {}\n");
    ecs_os_free(out);

    remove("Script_patch_multiple_hunks.flecs");
    ecs_fini(world);
}

void Script_patch_moved_lines(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_moved_lines.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\nC {}\nD {}\nE {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_moved_lines.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_moved_lines.flecs "
        "@@ -1,2 +1,2 @@\n"
        " D {}\n"
        "-E {}\n"
        "+E2 {}\n", &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_moved_lines.flecs", &error);
    test_str(out, "A {}\nB {}\nC {}\nD {}\nE2 {}\n");
    ecs_os_free(out);

    remove("Script_patch_moved_lines.flecs");
    ecs_fini(world);
}

void Script_patch_no_line_numbers(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_no_line_numbers.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\nC {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_no_line_numbers.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_no_line_numbers.flecs "
        "@@\n"
        " B {}\n"
        "-C {}\n"
        "+Z {}\n", &error);
    test_bool(error, false);
    test_str(out, "ok");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_no_line_numbers.flecs", &error);
    test_str(out, "A {}\nB {}\nZ {}\n");
    ecs_os_free(out);

    remove("Script_patch_no_line_numbers.flecs");
    ecs_fini(world);
}

void Script_patch_mismatch(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_mismatch.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_mismatch.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_mismatch.flecs "
        "@@ -1,2 +1,2 @@\n"
        " A {}\n"
        "-Q {}\n"
        "+R {}\n", &error);
    test_bool(error, true);
    test_str(out, "error: hunk 1 does not apply");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_mismatch.flecs", &error);
    test_str(out, "A {}\nB {}\n");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "B") != 0);

    remove("Script_patch_mismatch.flecs");
    ecs_fini(world);
}

void Script_patch_no_hunks(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_no_hunks.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_no_hunks.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_no_hunks.flecs -A {}\n+B {}", &error);
    test_bool(error, true);
    test_str(out, "error: patch has no hunks");
    ecs_os_free(out);

    remove("Script_patch_no_hunks.flecs");
    ecs_fini(world);
}

void Script_patch_parse_error(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_parse_error.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_parse_error.flecs" }) != 0);

    ecs_log_set_level(-4);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_parse_error.flecs "
        "@@ -1 +1 @@\n"
        "-A {}\n"
        "+A {\n", &error);
    test_bool(error, true);
    test_assert(!strncmp(out, "error: ", 7));
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_read Script_patch_parse_error.flecs", &error);
    test_str(out, "A {}\n");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "A") != 0);

    remove("Script_patch_parse_error.flecs");
    ecs_fini(world);
}

void Script_patch_writes_file(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_writes_file.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_writes_file.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_writes_file.flecs "
        "@@ -2 +2 @@\n"
        "-B {}\n"
        "+C {}\n", &error);
    test_bool(error, false);
    ecs_os_free(out);

    char buf[64] = {0};
    f = fopen("Script_patch_writes_file.flecs", "r");
    test_assert(f != NULL);
    test_assert(fread(buf, 1, sizeof(buf) - 1, f) > 0);
    fclose(f);
    test_str(buf, "A {}\nC {}\n");

    remove("Script_patch_writes_file.flecs");
    ecs_fini(world);
}

void Script_patch_keeps_missing_newline(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_keeps_missing_newline.flecs", "w");
    test_assert(f != NULL);
    fputs("A {}\nB {}", f);
    fclose(f);
    test_assert(ecs_script(world,
        { .filename = "Script_patch_keeps_missing_newline.flecs" }) != 0);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "script_patch Script_patch_keeps_missing_newline.flecs "
        "@@ -2 +2 @@\n"
        "-B {}\n"
        "\\ No newline at end of file\n"
        "+C {}\n"
        "\\ No newline at end of file\n", &error);
    test_bool(error, false);
    ecs_os_free(out);

    out = box_terminal_run(world, s,
        "script_read Script_patch_keeps_missing_newline.flecs", &error);
    test_str(out, "A {}\nC {}");
    ecs_os_free(out);

    remove("Script_patch_keeps_missing_newline.flecs");
    ecs_fini(world);
}

void Script_patch_unknown_script(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "script_patch ../nope.flecs @@ -1 +1 @@\n-A {}\n+B {}", &error);
    test_bool(error, true);
    test_str(out, "error: '../nope.flecs' is outside the working directory");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "script_patch nope.flecs @@ -1 +1 @@\n-A {}\n+B {}",
        &error);
    test_bool(error, true);
    test_str(out, "error: no script 'nope.flecs'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Script_patch_http_post(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    FILE *f = fopen("Script_patch_http_post.flecs", "w");
    test_assert(f != NULL);
    fputs("A {\n  box.transform.Position: {1, 2, 3}\n}\n", f);
    fclose(f);
    test_assert(ecs_script(world, { .filename = "Script_patch_http_post.flecs" }) != 0);

    box_terminal_new(world);
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "POST", "/terminal/exec?session=s1",
        "script_patch Script_patch_http_post.flecs\r\n"
        "@@ -1,3 +1,3 @@\r\n"
        " A {\r\n"
        "-  box.transform.Position: {1, 2, 3}\r\n"
        "+  box.transform.Position: {7, 8, 9}\r\n"
        " }\r\n", &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "ok");
    ecs_os_free(body);

    ecs_entity_t a = ecs_lookup(world, "A");
    test_assert(a != 0);
    const Position *p = ecs_get(world, a, Position);
    test_assert(p != NULL);
    test_flt(p->x, 7);
    test_flt(p->y, 8);
    test_flt(p->z, 9);

    remove("Script_patch_http_post.flecs");
    ecs_fini(world);
}

void Script_commands_are_script_functions(void) {
    // command/scripts
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_lookup(world, "box.command.commands.script_read") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.script_write") != 0);
    test_assert(ecs_lookup(world, "box.command.commands.script_grep") != 0);

    ecs_entity_t fn = ecs_lookup(world, "box.command.commands.script_patch");
    test_assert(fn != 0);
    test_assert(ecs_has(world, fn, EcsScriptFunction));

    ecs_fini(world);
}
