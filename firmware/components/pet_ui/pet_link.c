/* M5a：device 侧事件线解析（NDJSON → 事件 + 卡片）。
 * 契约：docs/contracts/C05-event-protocol.md（2026-10-31 冻结）；
 * 来源：派生自上游 ESP32-buddy（MIT）pet_link.c 的行为与实现，按本项目 C05 增补
 * `agent` 字段（顶层单卡 + tasks 项，C03 §7 R2）。
 * 只支持：单层 JSON 对象 + 字符串/非负整数/true/false/null；
 * tasks 数组可解析（≤3 卡），其余数组/对象仅作未知键跳过（不报 JSON 错）。 */
#include <string.h>

#include "pet_link.h"

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

/* 解析 JSON 字符串；截断到 cap-1 并存 NUL。返回字符串之后的位置，失败返回 NULL。 */
static const char *parse_string(const char *p, const char *end, char *out, size_t cap)
{
    if (p >= end || *p != '"') {
        return NULL;
    }
    p++;
    size_t n = 0;
    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    while (p < end) {
        char c = *p;
        if (c == '"') {
            if (out != NULL && cap > 0) {
                out[(n < cap - 1) ? n : cap - 1] = '\0';
            }
            return p + 1;
        }
        if (c == '\\') {
            p++;
            if (p >= end) {
                return NULL;
            }
            switch (*p) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            default:   return NULL; /* 不支持 \u 等 */
            }
        } else if ((unsigned char)c < 0x20u) {
            return NULL; /* 裸控制字符非法 */
        }
        if (out != NULL && cap > 0 && n < cap - 1) {
            out[n] = c;
        }
        n++;
        p++;
    }
    return NULL; /* 未闭合 */
}

/* 解析非负整数。失败返回 NULL（调用方按字段决定错误码）。 */
static const char *parse_uint(const char *p, const char *end, long *out)
{
    if (p >= end || *p < '0' || *p > '9') {
        return NULL;
    }
    long v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 1000000000L) {
            return NULL;
        }
        p++;
    }
    *out = v;
    return p;
}

/* 未知键值跳过的最大嵌套深度：行输入不可信，且 rx 任务栈仅 4 KB（M4 §4.7 不崩） */
#define SKIP_MAX_DEPTH 8

/* 跳过任意 JSON 值；M8 起数组/对象也可跳过（未知键不再报 JSON 错） */
static const char *skip_value(const char *p, const char *end, int depth);

static const char *skip_object(const char *p, const char *end, int depth)
{
    if (depth <= 0) {
        return NULL;
    }
    p++; /* '{' 由调用方确认 */
    p = skip_ws(p, end);
    if (p < end && *p == '}') {
        return p + 1;
    }
    for (;;) {
        p = skip_ws(p, end);
        p = parse_string(p, end, NULL, 0); /* 键 */
        if (p == NULL) {
            return NULL;
        }
        p = skip_ws(p, end);
        if (p >= end || *p != ':') {
            return NULL;
        }
        p = skip_value(p + 1, end, depth - 1);
        if (p == NULL) {
            return NULL;
        }
        p = skip_ws(p, end);
        if (p < end && *p == ',') {
            p++;
            continue;
        }
        if (p < end && *p == '}') {
            return p + 1;
        }
        return NULL;
    }
}

static const char *skip_array(const char *p, const char *end, int depth)
{
    if (depth <= 0) {
        return NULL;
    }
    p++; /* '[' 由调用方确认 */
    p = skip_ws(p, end);
    if (p < end && *p == ']') {
        return p + 1;
    }
    for (;;) {
        p = skip_ws(p, end);
        p = skip_value(p, end, depth - 1);
        if (p == NULL) {
            return NULL;
        }
        p = skip_ws(p, end);
        if (p < end && *p == ',') {
            p++;
            continue;
        }
        if (p < end && *p == ']') {
            return p + 1;
        }
        return NULL;
    }
}

/* 跳过**仅** 字符串/数字/true/false/null/对象/数组 的值 */
static const char *skip_value(const char *p, const char *end, int depth)
{
    if (p >= end) {
        return NULL;
    }
    if (*p == '"') {
        return parse_string(p, end, NULL, 0);
    }
    if (*p == '{') {
        return skip_object(p, end, depth);
    }
    if (*p == '[') {
        return skip_array(p, end, depth);
    }
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        if (*p == '-') {
            p++;
        }
        if (p >= end || *p < '0' || *p > '9') {
            return NULL;
        }
        while (p < end && *p >= '0' && *p <= '9') {
            p++;
        }
        /* 小数/指数：仅为跳过，简单接受 */
        if (p < end && *p == '.') {
            p++;
            while (p < end && *p >= '0' && *p <= '9') {
                p++;
            }
        }
        return p;
    }
    if (end - p >= 4 && memcmp(p, "true", 4) == 0) {
        return p + 4;
    }
    if (end - p >= 5 && memcmp(p, "false", 5) == 0) {
        return p + 5;
    }
    if (end - p >= 4 && memcmp(p, "null", 4) == 0) {
        return p + 4;
    }
    return NULL;
}

static bool map_event(const char *name, pet_event_kind_t *kind, pet_state_t *state)
{
    if (strcmp(name, "task.start") == 0) {
        *kind = PET_EV_TASK_START;
        *state = PET_STATE_WORKING;
    } else if (strcmp(name, "agent.input_required") == 0) {
        *kind = PET_EV_AGENT_INPUT_REQUIRED;
        *state = PET_STATE_INPUT_REQUIRED;
    } else if (strcmp(name, "task.done") == 0) {
        *kind = PET_EV_TASK_DONE;
        *state = PET_STATE_DONE;
    } else if (strcmp(name, "task.error") == 0 || strcmp(name, "agent.blocked") == 0) {
        *kind = PET_EV_TASK_ERROR;
        *state = PET_STATE_FAILED;
    } else if (strcmp(name, "agent.idle") == 0) {
        *kind = PET_EV_AGENT_IDLE;
        *state = PET_STATE_IDLE;
    } else if (strcmp(name, "device.heartbeat") == 0) {
        *kind = PET_EV_ANNOUNCEMENT; /* 只刷新链路 */
        *state = PET_STATE_IDLE;
    } else {
        return false;
    }
    return true;
}

/* status 字符串 → 6 态；缺省/working/未知 → WORKING（M8 §8.1） */
static pet_state_t map_status(const char *s)
{
    if (strcmp(s, "input_required") == 0) {
        return PET_STATE_INPUT_REQUIRED;
    }
    if (strcmp(s, "done") == 0) {
        return PET_STATE_DONE;
    }
    if (strcmp(s, "failed") == 0) {
        return PET_STATE_FAILED;
    }
    return PET_STATE_WORKING;
}

/* 解析 tasks 的一项（'{' 已确认）。返回项 '}' 之后的位置；语法非法 → NULL
 * （整行按 JSON 错）。字段类型不符 → 仍消费完该对象，但 *ok=false（项忽略）。 */
static const char *parse_task_item(const char *p, const char *end,
                                   pet_card_t *card, bool *ok)
{
    *ok = false;
    pet_card_t c;
    memset(&c, 0, sizeof c);
    c.state = PET_STATE_WORKING;
    char status[24] = {0};
    bool has_status = false;
    bool type_ok = true;

    p = skip_ws(p + 1, end);
    if (p < end && *p == '}') {
        *card = c;
        *ok = true;
        return p + 1;
    }
    for (;;) {
        char key[16];
        p = skip_ws(p, end);
        const char *q = parse_string(p, end, key, sizeof key);
        if (q == NULL) {
            return NULL;
        }
        p = skip_ws(q, end);
        if (p >= end || *p != ':') {
            return NULL;
        }
        p = skip_ws(p + 1, end);

        if (strcmp(key, "title") == 0) {
            q = parse_string(p, end, c.title, sizeof c.title);
            if (q == NULL) {
                type_ok = false;
                q = skip_value(p, end, SKIP_MAX_DEPTH);
            }
        } else if (strcmp(key, "project") == 0) {
            q = parse_string(p, end, c.project, sizeof c.project);
            if (q == NULL) {
                type_ok = false;
                q = skip_value(p, end, SKIP_MAX_DEPTH);
            }
        } else if (strcmp(key, "agent") == 0) {
            q = parse_string(p, end, c.agent, sizeof c.agent);
            if (q == NULL) {
                type_ok = false;
                q = skip_value(p, end, SKIP_MAX_DEPTH);
            }
        } else if (strcmp(key, "elapsed_s") == 0) {
            long elapsed = 0;
            q = parse_uint(p, end, &elapsed);
            if (q == NULL) {
                type_ok = false;
                q = skip_value(p, end, SKIP_MAX_DEPTH);
            } else {
                c.elapsed_s = (int32_t)elapsed;
            }
        } else if (strcmp(key, "status") == 0) {
            q = parse_string(p, end, status, sizeof status);
            if (q == NULL) {
                type_ok = false;
                q = skip_value(p, end, SKIP_MAX_DEPTH);
            } else {
                has_status = true;
            }
        } else {
            q = skip_value(p, end, SKIP_MAX_DEPTH); /* 项内未知键 */
        }
        if (q == NULL) {
            return NULL;
        }
        p = skip_ws(q, end);
        if (p < end && *p == ',') {
            p++;
            continue;
        }
        if (p < end && *p == '}') {
            p++;
            break;
        }
        return NULL;
    }
    if (type_ok) {
        c.state = has_status ? map_status(status) : PET_STATE_WORKING;
        *card = c;
        *ok = true;
    }
    return p;
}

/* 解析 tasks 数组（'[' 已确认）。取前 3 个对象项；非对象项跳过。语法非法 → NULL。 */
static const char *parse_tasks_array(const char *p, const char *end,
                                     pet_link_msg_t *out)
{
    p = skip_ws(p + 1, end);
    if (p < end && *p == ']') {
        return p + 1;
    }
    for (;;) {
        p = skip_ws(p, end);
        if (p < end && *p == '{') {
            pet_card_t c;
            bool ok = false;
            p = parse_task_item(p, end, &c, &ok);
            if (p == NULL) {
                return NULL;
            }
            if (ok && out->card_count < 3) {
                out->cards[out->card_count++] = c;
            }
        } else {
            p = skip_value(p, end, SKIP_MAX_DEPTH); /* 非对象项：跳过 */
            if (p == NULL) {
                return NULL;
            }
        }
        p = skip_ws(p, end);
        if (p < end && *p == ',') {
            p++;
            continue;
        }
        if (p < end && *p == ']') {
            return p + 1;
        }
        return NULL;
    }
}

pet_link_status_t pet_link_parse(const char *line, size_t len, pet_link_msg_t *out)
{
    if (out == NULL) {
        return PET_LINK_ERR_JSON;
    }
    memset(out, 0, sizeof *out);
    if (line == NULL) {
        return PET_LINK_ERR_JSON;
    }
    if (len > PET_LINE_MAX) {
        return PET_LINK_ERR_TOO_LONG;
    }

    const char *p = line;
    const char *end = line + len;

    p = skip_ws(p, end);
    if (p >= end || *p != '{') {
        return PET_LINK_ERR_JSON;
    }
    p++;

    long version = -1;
    bool has_version = false;
    bool has_event = false;
    char event[48] = {0};
    char title[PET_TITLE_MAX] = {0};
    char project[PET_PROJECT_MAX] = {0};
    char agent[PET_AGENT_MAX] = {0};
    bool has_title = false;
    bool has_project = false;
    bool has_agent = false;
    bool has_elapsed = false;
    long elapsed = 0;

    p = skip_ws(p, end);
    if (p < end && *p == '}') {
        p++;
    } else {
        for (;;) {
            p = skip_ws(p, end);
            char key[32];
            const char *q = parse_string(p, end, key, sizeof key);
            if (q == NULL) {
                return PET_LINK_ERR_JSON;
            }
            p = skip_ws(q, end);
            if (p >= end || *p != ':') {
                return PET_LINK_ERR_JSON;
            }
            p = skip_ws(p + 1, end);

            if (strcmp(key, "version") == 0) {
                const char *vend = parse_uint(p, end, &version);
                if (vend == NULL) {
                    return PET_LINK_ERR_VERSION;
                }
                has_version = true;
                p = vend;
            } else if (strcmp(key, "event") == 0) {
                q = parse_string(p, end, event, sizeof event);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE;
                }
                has_event = true;
                p = q;
            } else if (strcmp(key, "title") == 0) {
                q = parse_string(p, end, title, sizeof title);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE;
                }
                has_title = true;
                p = q;
            } else if (strcmp(key, "project") == 0) {
                q = parse_string(p, end, project, sizeof project);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE;
                }
                has_project = true;
                p = q;
            } else if (strcmp(key, "agent") == 0) {
                q = parse_string(p, end, agent, sizeof agent);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE;
                }
                has_agent = true;
                p = q;
            } else if (strcmp(key, "elapsed_s") == 0) {
                q = parse_uint(p, end, &elapsed);
                if (q == NULL) {
                    return PET_LINK_ERR_TYPE; /* 负数/字符串/非数字 */
                }
                has_elapsed = true;
                p = q;
            } else if (strcmp(key, "tasks") == 0) {
                p = skip_ws(p, end);
                if (p < end && *p == '[') {
                    out->has_tasks = true; /* 行内出现 tasks（含 []）→ 整栈替换/清空 */
                    p = parse_tasks_array(p, end, out);
                    if (p == NULL) {
                        return PET_LINK_ERR_JSON; /* 数组语法坏 */
                    }
                } else {
                    q = skip_value(p, end, SKIP_MAX_DEPTH); /* 非数组 → 忽略该键（M8 §8.1） */
                    if (q == NULL) {
                        return PET_LINK_ERR_JSON;
                    }
                    p = q;
                }
            } else {
                q = skip_value(p, end, SKIP_MAX_DEPTH);
                if (q == NULL) {
                    return PET_LINK_ERR_JSON; /* 未知键的嵌套/数组不支持 */
                }
                p = q;
            }

            p = skip_ws(p, end);
            if (p < end && *p == ',') {
                p++;
                continue;
            }
            if (p < end && *p == '}') {
                p++;
                break;
            }
            return PET_LINK_ERR_JSON;
        }
    }

    p = skip_ws(p, end);
    if (p != end) {
        return PET_LINK_ERR_JSON; /* 尾部多余内容 */
    }
    if (!has_version || version != 1) {
        return PET_LINK_ERR_VERSION;
    }
    if (!has_event) {
        return PET_LINK_ERR_EVENT;
    }

    pet_event_kind_t kind;
    pet_state_t mapped;
    if (!map_event(event, &kind, &mapped)) {
        return PET_LINK_ERR_UNKNOWN_EVENT;
    }

    out->kind = kind;
    if (has_title || has_project || has_agent || has_elapsed) {
        out->has_card = true;
        if (has_title) {
            memcpy(out->card.title, title, sizeof out->card.title);
        }
        if (has_project) {
            memcpy(out->card.project, project, sizeof out->card.project);
        }
        if (has_agent) {
            memcpy(out->card.agent, agent, sizeof out->card.agent);
        }
        if (has_elapsed) {
            out->card.elapsed_s = (int32_t)elapsed;
        }
        out->card.state = mapped;
    }
    return PET_LINK_OK;
}
