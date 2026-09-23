/**
 * @file editor/source.c
 * @brief What a script assigns to a component of an entity, and writing an edit back.
 *
 * The statement of the entity is found through ecs_script_entity_source and
 * scanned for the component at its own scope depth.
 *
 * A value made of numbers, strings, booleans, braces and member keys is literal;
 * an identifier used as a value, an operator or a call makes it computed.
 */

#include "editor_internal.h"
#include <string.h>
#include <ctype.h>

/** Tell whether a byte may be part of an identifier or a path.
 *
 * @param c The byte
 * @return true for letters, digits, underscores and dots
 */
static bool box_editor_ident(
    char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

/** Skip spaces and tabs.
 *
 * @param cur The position
 * @param end End of the text
 * @return The first byte that is not a space or a tab
 */
static const char* box_editor_skip_blank(
    const char *cur,
    const char *end)
{
    while (cur < end && (cur[0] == ' ' || cur[0] == '\t')) {
        cur ++;
    }

    return cur;
}

/** Test whether a statement at a position assigns a component.
 *
 * @param cur Start of the statement
 * @param end End of the text
 * @param name Name of the component, the last segment of its path
 * @param value_out Receives the start of the value
 * @return true when the statement is `name:` or `path.name:`
 */
static bool box_editor_statement_is(
    const char *cur,
    const char *end,
    const char *name,
    const char **value_out)
{
    const char *start = cur;
    bool result = false;
    while (cur < end && box_editor_ident(cur[0])) {
        cur ++;
    }

    const char *ident_end = cur;
    cur = box_editor_skip_blank(cur, end);
    if (cur >= end || cur[0] != ':' || ident_end == start) {
        goto done;
    }

    const char *last = ident_end;
    while (last > start && last[-1] != '.') {
        last --;
    }

    size_t length = (size_t)(ident_end - last);
    if (length != strlen(name) || strncmp(last, name, length)) {
        goto done;
    }

    *value_out = box_editor_skip_blank(cur + 1, end);
    result = true;
done:
    return result;
}

/** End of a value: the matching brace of an initializer or the end of the line.
 *
 * @param cur Start of the value
 * @param end End of the text
 * @return The byte after the value
 */
static const char* box_editor_value_end(
    const char *cur,
    const char *end)
{
    int32_t depth = 0;
    bool string = false;
    while (cur < end) {
        char c = cur[0];
        if (string) {
            if (c == '\\' && cur + 1 < end) {
                cur ++;
            } else if (c == '"') {
                string = false;
            }
        } else if (c == '"') {
            string = true;
        } else if (c == '{' || c == '[' || c == '(') {
            depth ++;
        } else if (c == '}' || c == ']' || c == ')') {
            depth --;
            if (depth <= 0) {
                cur ++;
                break;
            }
        } else if (!depth && (c == '\n' || c == ';')) {
            break;
        }

        cur ++;
    }

    return cur;
}

/** Tell whether a value is made of literals only.
 *
 * @param cur Start of the value
 * @param end End of the value
 * @return true when nothing in the value is computed
 */
static bool box_editor_value_literal(
    const char *cur,
    const char *end)
{
    bool result = true;
    bool value_start = true;
    while (cur < end && result) {
        char c = cur[0];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            cur ++;
        } else if (c == '{' || c == '[' || c == ',' || c == ':' || c == '}' || c == ']') {
            value_start = c != '}' && c != ']';
            cur ++;
        } else if (c == '"') {
            cur ++;
            while (cur < end && cur[0] != '"') {
                cur += cur[0] == '\\' ? 2 : 1;
            }

            cur ++;
            value_start = false;
        } else if (isdigit((unsigned char)c) || ((c == '-' || c == '+') && value_start &&
            cur + 1 < end && (isdigit((unsigned char)cur[1]) || cur[1] == '.')) ||
            (c == '.' && cur + 1 < end && isdigit((unsigned char)cur[1])))
        {
            cur ++;
            while (cur < end && (isalnum((unsigned char)cur[0]) || cur[0] == '.' ||
                ((cur[0] == '-' || cur[0] == '+') && (cur[-1] == 'e' || cur[-1] == 'E'))))
            {
                cur ++;
            }

            value_start = false;
        } else if (isalpha((unsigned char)c) || c == '_') {
            const char *word = cur;
            while (cur < end && (isalnum((unsigned char)cur[0]) || cur[0] == '_')) {
                cur ++;
            }

            size_t length = (size_t)(cur - word);
            const char *after = box_editor_skip_blank(cur, end);
            bool key = after < end && after[0] == ':';
            bool keyword = (length == 4 && !strncmp(word, "true", 4)) ||
                (length == 5 && !strncmp(word, "false", 5));
            result = key || keyword;
            value_start = false;
        } else {
            result = false;
        }
    }

    return result;
}

/** Find the value a statement span assigns to a component at scope depth one.
 *
 * @param code Start of the statement of the entity
 * @param end End of the statement
 * @param name Name of the component
 * @param value_end Receives the end of the value
 * @return Start of the value, NULL when the statement does not assign it
 */
static const char* box_editor_find_value(
    const char *code,
    const char *end,
    const char *name,
    const char **value_end)
{
    const char *cur = code;
    const char *result = NULL;
    int32_t depth = 0;
    bool statement = false;
    while (cur < end && !result) {
        char c = cur[0];
        if (c == '{') {
            depth ++;
            statement = true;
            cur ++;
        } else if (c == '}') {
            depth --;
            cur ++;
        } else if (c == '\n' || c == ';') {
            statement = true;
            cur ++;
        } else if (c == '"') {
            cur ++;
            while (cur < end && cur[0] != '"') {
                cur += cur[0] == '\\' ? 2 : 1;
            }

            cur ++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            cur ++;
        } else if (depth == 1 && statement) {
            const char *value;
            if (box_editor_statement_is(cur, end, name, &value)) {
                result = value;
                *value_end = box_editor_value_end(value, end);
            } else {
                statement = false;
                cur ++;
            }
        } else {
            statement = false;
            cur ++;
        }
    }

    return result;
}

box_editor_source_t box_editor_source(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component)
{
    box_editor_source_t result = BoxEditorSourceNone;
    ecs_entity_t owner = ecs_script_entity_owner(world, e);
    const EcsScript *script = owner ? ecs_get(world, owner, EcsScript) : NULL;
    ecs_script_source_t source;
    if (!script || !script->script || !ecs_script_entity_source(script->script, e, &source)) {
        goto done;
    }

    const char *code = script->script->code + source.offset;
    const char *end = code + source.length;
    const char *name = ecs_get_name(world, component);
    const char *value_end = NULL;
    const char *value = name ? box_editor_find_value(code, end, name, &value_end) : NULL;
    if (!value) {
        goto done;
    }

    result = box_editor_value_literal(value, value_end) ?
        BoxEditorSourceLiteral : BoxEditorSourceComputed;
done:
    return result;
}

/** Tell whether an entity was created by the body of a template.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity carries the ScriptTemplate pair
 */
static bool box_editor_template_child(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_get_target(world, e, EcsScriptTemplate, 0) != 0;
}

bool box_editor_editable(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component)
{
    bool result = true;
    if (!box_editor_template_child(world, e)) {
        goto done;
    }

    result = box_editor_source(world, e, component) != BoxEditorSourceComputed;
done:
    return result;
}

bool box_editor_write_through(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t component)
{
    bool result = false;
    if (!box_editor_template_child(world, e) ||
        box_editor_source(world, e, component) != BoxEditorSourceLiteral)
    {
        goto done;
    }

    ecs_entity_t owner = ecs_script_entity_owner(world, e);
    const EcsScript *script = owner ? ecs_get(world, owner, EcsScript) : NULL;
    if (!script || !script->script) {
        goto done;
    }

    ecs_script_edits_t *edits = ecs_script_edits_new(script->script);
    if (!edits) {
        goto done;
    }

    const void *value = ecs_get_id(world, e, component);
    if (ecs_script_edits_set(edits, e, component, value)) {
        ecs_script_edits_free(edits);
        goto done;
    }

    char *code = ecs_script_edits_apply(edits);
    ecs_script_edits_free(edits);
    if (!code) {
        goto done;
    }

    result = ecs_script_reload(world, owner, code) == 0;
    ecs_os_free(code);
done:
    return result;
}
