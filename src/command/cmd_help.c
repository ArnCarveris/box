/**
 * @file command/cmd_help.c
 * @brief The help command: list the commands of box.command.commands or show how to use one.
 *
 * The commands are read from the scope when help runs, so commands that other modules or the
 * application register are listed like the builtin ones.
 */

#include "command.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/** Most commands an unknown name suggests */
#define BOX_HELP_SUGGESTIONS (3)

/** Spaces between the name column and the brief */
#define BOX_HELP_GAP (2)

/** A command of the scope */
typedef struct box_help_entry_t {
    char *name;         /**< Path from the scope, dotted */
    const char *brief;  /**< The flecs doc brief, NULL when the command has none */
    ecs_entity_t cmd;   /**< The command function */
    int32_t score;      /**< Rank of a suggestion, lower is closer */
} box_help_entry_t;

/** Get the length of the group of a command name, the part before the first dot.
 *
 * @param name The command name
 * @return Length of the group, -1 for an ungrouped command
 */
static int32_t box_help_group(
    const char *name)
{
    const char *dot = strchr(name, '.');

    return dot ? (int32_t)(dot - name) : -1;
}

/** Order commands by name with the ungrouped ones first and each group together.
 *
 * @param ptr_a The first entry
 * @param ptr_b The second entry
 * @return Negative, zero or positive like strcmp
 */
static int box_help_compare(
    const void *ptr_a,
    const void *ptr_b)
{
    const box_help_entry_t *a = ptr_a, *b = ptr_b;
    int32_t ga = box_help_group(a->name), gb = box_help_group(b->name);
    int result = (ga >= 0) - (gb >= 0);
    if (!result && ga >= 0) {
        result = strncmp(a->name, b->name, (size_t)(ga < gb ? ga : gb));
        if (!result) {
            result = ga - gb;
        }
    }

    if (!result) {
        result = strcmp(a->name, b->name);
    }

    return result;
}

/** Order suggestions by score, then like the list.
 *
 * @param ptr_a The first entry
 * @param ptr_b The second entry
 * @return Negative, zero or positive like strcmp
 */
static int box_help_compare_score(
    const void *ptr_a,
    const void *ptr_b)
{
    const box_help_entry_t *a = ptr_a, *b = ptr_b;
    int result = a->score - b->score;
    if (!result) {
        result = box_help_compare(a, b);
    }

    return result;
}

/** Add the commands under an entity of the scope to a list.
 *
 * @param world The world
 * @param scope The command scope
 * @param parent The entity whose children are visited
 * @param out The box_help_entry_t list
 */
static void box_help_collect(
    const ecs_world_t *world,
    ecs_entity_t scope,
    ecs_entity_t parent,
    ecs_vec_t *out)
{
    ecs_iter_t it = ecs_children(world, parent);
    int32_t i;
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (ecs_has(world, e, CommandInfo) && ecs_has(world, e, EcsScriptFunction)) {
                box_help_entry_t *entry = ecs_vec_append_t(NULL, out, box_help_entry_t);
                *entry = (box_help_entry_t){
                    .name = ecs_get_path_w_sep(world, scope, e, ".", NULL),
                    .brief = ecs_doc_get_brief(world, e),
                    .cmd = e
                };
            }

            box_help_collect(world, scope, e, out);
        }
    }
}

/** Get the commands of the scope sorted by name and grouped.
 *
 * @param world The world
 * @param out Receives the box_help_entry_t list, freed with box_help_fini
 */
static void box_help_commands(
    const ecs_world_t *world,
    ecs_vec_t *out)
{
    ecs_entity_t scope = ecs_lookup(world, BOX_COMMAND_SCOPE);
    ecs_vec_init_t(NULL, out, box_help_entry_t, 0);
    if (scope) {
        box_help_collect(world, scope, scope, out);
    }

    if (ecs_vec_count(out)) {
        qsort(ecs_vec_first(out), (size_t)ecs_vec_count(out), sizeof(box_help_entry_t),
            box_help_compare);
    }
}

/** Free a list of box_help_commands.
 *
 * @param list The list
 */
static void box_help_fini(
    ecs_vec_t *list)
{
    box_help_entry_t *entries = ecs_vec_first(list);
    int32_t i;
    for (i = 0; i < ecs_vec_count(list); i ++) {
        ecs_os_free(entries[i].name);
    }

    ecs_vec_fini_t(NULL, list, box_help_entry_t);
}

/** Append commands one per line, name padded to one column and brief, a blank line between
 * groups.
 *
 * @param entries The commands, sorted
 * @param count Number of commands
 * @param buf The buffer
 */
static void box_help_list(
    const box_help_entry_t *entries,
    int32_t count,
    ecs_strbuf_t *buf)
{
    int32_t i, width = 0;
    for (i = 0; i < count; i ++) {
        int32_t len = ecs_os_strlen(entries[i].name);
        width = len > width ? len : width;
    }

    for (i = 0; i < count; i ++) {
        const char *name = entries[i].name;
        int32_t group = box_help_group(name);
        if (i) {
            const char *prev = entries[i - 1].name;
            bool same = group == box_help_group(prev) &&
                (group < 0 || !strncmp(name, prev, (size_t)group));
            ecs_strbuf_appendstr(buf, same ? "\n" : "\n\n");
        }

        if (entries[i].brief && entries[i].brief[0]) {
            ecs_strbuf_append(buf, "%-*s%s", width + BOX_HELP_GAP, name, entries[i].brief);
        } else {
            ecs_strbuf_appendstr(buf, name);
        }
    }
}

/** Get the edit distance of two names, a swap of neighboring characters counting as one edit.
 *
 * @param a The first name
 * @param a_len Characters of a
 * @param b The second name
 * @param b_len Characters of b
 * @return The number of edits that turn a into b, case insensitive
 */
static int32_t box_help_distance(
    const char *a,
    int32_t a_len,
    const char *b,
    int32_t b_len)
{
    int32_t cols = b_len + 1, i, j;
    int32_t *d = ecs_os_malloc_n(int32_t, (a_len + 1) * cols);
    for (i = 0; i <= a_len; i ++) {
        for (j = 0; j <= b_len; j ++) {
            int32_t v = i > j ? i : j;
            if (i && j) {
                int32_t cost = tolower((unsigned char)a[i - 1]) !=
                    tolower((unsigned char)b[j - 1]);
                int32_t del = d[(i - 1) * cols + j] + 1, ins = d[i * cols + j - 1] + 1;
                v = d[(i - 1) * cols + j - 1] + cost;
                v = del < v ? del : v;
                v = ins < v ? ins : v;
                if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                    d[(i - 2) * cols + j - 2] + 1 < v)
                {
                    v = d[(i - 2) * cols + j - 2] + 1;
                }
            } else {
                v = i + j;
            }

            d[i * cols + j] = v;
        }
    }

    int32_t result = d[a_len * cols + b_len];
    ecs_os_free(d);

    return result;
}

/** Score how close a command name is to an unknown name.
 *
 * Names that start with the unknown name score 0, others score their edit distance to the
 * whole name or to the part after the last dot, -1 when that is too far to suggest.
 *
 * @param name The unknown name
 * @param cmd The command name
 * @return The score, -1 when the command is not a suggestion
 */
static int32_t box_help_score(
    const char *name,
    const char *cmd)
{
    int32_t len = ecs_os_strlen(name), cmd_len = ecs_os_strlen(cmd);
    int32_t max = len < 4 ? 1 : len < 8 ? 2 : 3, result = -1;
    const char *last = strrchr(cmd, '.');
    if (len && !strncmp(cmd, name, (size_t)len)) {
        result = 0;
    } else if (len) {
        int32_t d = box_help_distance(name, len, cmd, cmd_len);
        if (last) {
            int32_t d_last = box_help_distance(name, len, last + 1, ecs_os_strlen(last + 1));
            d = d_last < d ? d_last : d;
        }

        result = d <= max ? d + 1 : -1;
    }

    return result;
}

/** Get the error of an unknown name with up to three commands of the closest names.
 *
 * @param world The world
 * @param name The unknown name
 * @return The message without the BOX_COMMAND_ERROR prefix, which the caller frees
 */
static char* box_help_unknown(
    const ecs_world_t *world,
    const char *name)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_vec_t list, found;
    int32_t i, count;
    box_help_commands(world, &list);
    ecs_vec_init_t(NULL, &found, box_help_entry_t, 0);
    box_help_entry_t *entries = ecs_vec_first(&list);
    for (i = 0; i < ecs_vec_count(&list); i ++) {
        int32_t score = box_help_score(name, entries[i].name);
        if (score >= 0) {
            box_help_entry_t *s = ecs_vec_append_t(NULL, &found, box_help_entry_t);
            *s = entries[i];
            s->score = score;
        }
    }

    count = ecs_vec_count(&found);
    if (count) {
        qsort(ecs_vec_first(&found), (size_t)count, sizeof(box_help_entry_t),
            box_help_compare_score);
    }

    ecs_strbuf_append(&buf, "unknown command '%s'", name);
    entries = ecs_vec_first(&found);
    for (i = 0; i < count && i < BOX_HELP_SUGGESTIONS; i ++) {
        ecs_strbuf_appendstr(&buf, i ? ", " : ", did you mean: ");
        ecs_strbuf_appendstr(&buf, entries[i].name);
    }

    ecs_vec_fini_t(NULL, &found, box_help_entry_t);
    box_help_fini(&list);

    return ecs_strbuf_get(&buf);
}

void box_command_usage(
    const ecs_world_t *world,
    ecs_entity_t cmd,
    ecs_strbuf_t *buf)
{
    const EcsScriptFunction *f = ecs_get(world, cmd, EcsScriptFunction);
    const CommandInfo *info = ecs_get(world, cmd, CommandInfo);
    const ecs_script_parameter_t *params = ecs_vec_first(&f->params);
    char *name = ecs_get_path_w_sep(world, ecs_lookup(world, BOX_COMMAND_SCOPE), cmd, ".", NULL);
    int32_t i, count = ecs_vec_count(&f->params);
    ecs_strbuf_append(buf, "usage: %s", name);
    for (i = 1; i < count; i ++) {
        const char *suffix = info->rest && i == count - 1 ? "..." : "";
        if (i > info->required) {
            ecs_strbuf_append(buf, " [%s%s]", params[i].name, suffix);
        } else {
            ecs_strbuf_append(buf, " <%s%s>", params[i].name, suffix);
        }
    }

    ecs_os_free(name);
}

/** Append the usage line, the brief and one line per parameter with its type.
 *
 * @param world The world
 * @param cmd The command function
 * @param buf The buffer
 */
static void box_help_command(
    const ecs_world_t *world,
    ecs_entity_t cmd,
    ecs_strbuf_t *buf)
{
    const EcsScriptFunction *f = ecs_get(world, cmd, EcsScriptFunction);
    const CommandInfo *info = ecs_get(world, cmd, CommandInfo);
    const ecs_script_parameter_t *params = ecs_vec_first(&f->params);
    const char *brief = ecs_doc_get_brief(world, cmd);
    int32_t i, width = 0, count = ecs_vec_count(&f->params);
    char *marked[FLECS_SCRIPT_FUNCTION_ARGS_MAX] = {0};
    box_command_usage(world, cmd, buf);
    if (brief && brief[0]) {
        ecs_strbuf_append(buf, "\n%s", brief);
    }

    for (i = 1; i < count; i ++) {
        const char *suffix = info->rest && i == count - 1 ? "..." : "";
        bool optional = i > info->required;
        marked[i] = flecs_asprintf("%c%s%s%c", optional ? '[' : '<', params[i].name, suffix,
            optional ? ']' : '>');
        int32_t len = ecs_os_strlen(marked[i]);
        width = len > width ? len : width;
    }

    for (i = 1; i < count; i ++) {
        ecs_strbuf_append(buf, "\n  %-*s%s", width + BOX_HELP_GAP, marked[i],
            ecs_get_name(world, params[i].type));
        ecs_os_free(marked[i]);
    }
}

/** The help command: list the commands, or show the usage and parameters of one.
 *
 * A name that is the group of commands, like mouse, lists the commands of the group.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the command name
 * @param result Receives the output
 */
static void box_cmd_help(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const ecs_world_t *world = ctx->world;
    const char *name = *(char**)argv[1].ptr;
    const box_help_entry_t *entries;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_entity_t scope = ecs_lookup(world, BOX_COMMAND_SCOPE), cmd = 0;
    ecs_vec_t list;
    int32_t i, first = 0, count = 0, matched = 0, len = name ? ecs_os_strlen(name) : 0;
    if (len) {
        cmd = ecs_lookup_path_w_sep(world, scope, name, ".", NULL, false);
    }

    if (cmd && ecs_has(world, cmd, CommandInfo) && ecs_has(world, cmd, EcsScriptFunction)) {
        box_help_command(world, cmd, &buf);
        *(char**)result->ptr = ecs_strbuf_get(&buf);
        goto done;
    }

    box_help_commands(world, &list);
    entries = ecs_vec_first(&list);
    count = ecs_vec_count(&list);
    len -= len && name[len - 1] == '.';
    if (len) {
        for (i = 0; i < count; i ++) {
            bool in_group = box_help_group(entries[i].name) == len &&
                !strncmp(entries[i].name, name, (size_t)len);
            first = in_group && !matched ? i : first;
            matched += in_group;
        }

        count = matched;
    }

    if (count) {
        box_help_list(entries + first, count, &buf);
        *(char**)result->ptr = ecs_strbuf_get(&buf);
    } else {
        char *msg = box_help_unknown(world, name);
        box_command_error(ctx, result, "%s", msg);
        ecs_os_free(msg);
    }

    box_help_fini(&list);
done:
    return;
}

void box_cmd_help_init(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "help",
        .brief = "List the commands or show how to use one",
        .params = {{ .name = "command", .type = ecs_id(ecs_string_t), .optional = true }},
        .callback = box_cmd_help,
        .quiet = true
    });
}
