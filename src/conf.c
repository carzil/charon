#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

#include "conf.h"
#include "defs.h"
#include "utils/logging.h"
#include "utils/array.h"
#include "utils/vector.h"

#define ASSERT_TOKEN(tok) do { if (st->token != tok) { return -CHARON_ERR; } } while (0)
#define ASSERT_TOKEN_F(tok, label) do { if (st->token != tok) { res = -CHARON_ERR; goto label; } } while (0)
#define EXPECT_TOKEN(tok) do { if ((res = read_token(st)) != CHARON_OK) { return res; } else if (st->token != tok) { return -CHARON_ERR; } } while (0)
#define EXPECT_TOKEN_F(tok, label) do { if ((res = read_token(st)) != CHARON_OK) { goto cleanup; } else if (st->token != tok) { res = -CHARON_ERR; goto cleanup; } } while (0)
#define NEXT_TOKEN() if ((res = read_token(st))) { return res; }
#define WHILE_TOKEN() while ((res = read_token(st)) == CHARON_OK)

enum {
    UNKNOWN_TOKEN = 11
};

typedef struct {
    enum {
        t_none,
        t_id,
        t_digit,
        t_semicolon,
        t_open_brace,
        t_close_brace,
        t_equal,
        t_string
    } token;
    array_t token_s;

    int fd;
    int pos;
    int size;
    char buf[4096];
    unsigned eof:1;
    char quote;

    int line;
    int line_pos;

    conf_def_t* defs;

    conf_section_def_t* current_section_def;
    void* current_section;
} config_state_t;

void config_state_init(config_state_t* state)
{
    state->pos = -1;
    state->eof = 0;
    state->size = 0;
    state->line = 1;
    state->line_pos = 0;
    array_init(&state->token_s, 10);
}

void config_state_destroy(config_state_t* st)
{
    array_destroy(&st->token_s);
}

#define SECTION_DEF_IS_NOT_NULL(sd) (sd->name != NULL)
#define FIELD_DEF_IS_NOT_NULL(fd) (fd->name != NULL)
#define CONF_DEF_IS_NOT_NULL(cd) (cd->conf != NULL)

void config_init(config_state_t* st)
{
    conf_def_t* conf_def = st->defs;
    conf_section_def_t* sect_def;

    while (CONF_DEF_IS_NOT_NULL(conf_def)) {
        sect_def = conf_def->sections;
        while (SECTION_DEF_IS_NOT_NULL(sect_def)) {
            if (sect_def->flags & CONF_ALLOW_MULTIPLE) {
                vector_init((char*)conf_def->conf + sect_def->offset);
            }
            sect_def++;
        }
        conf_def++;
    }
}

int read_buffer(config_state_t* st)
{
    int count;

    count = read(st->fd, st->buf, 4096);
    if (count < 0) {
        charon_perror("read: ");
        return -CHARON_ERR;
    } else if (count == 0) {
        st->size = 0;
        st->pos = 0;
        st->eof = 1;
        return 0;
    } else {
        st->size = count;
        st->pos = 0;
    }
    return CHARON_OK;
}

int read_char(config_state_t* st)
{
    int res;

    if (st->pos == st->size) {
        res = read_buffer(st);
        if (res < 0) {
            return res;
        }

        if (st->eof) {
            return -CHARON_EOF;
        }
    }
    if (st->buf[st->pos] == '\n') {
        st->line++;
        st->line_pos = 1;
    }
    st->line_pos++;
    return st->buf[st->pos++];
}

int _determine_token_type(config_state_t* st)
{
    char ch;

    if (st->pos == -1) {
        st->pos = 0;
        ch = read_char(st);
    } else {
        ch = st->buf[st->pos - 1];
    }

    // charon_debug("ch = '%c'", ch);

    while (ch > 0 && isspace(ch)) {
        ch = read_char(st);
    }

    if (ch < 0) {
        return ch;
    }

    if (isalpha(ch)) {
        st->token = t_id;
    } else if (isdigit(ch)) {
        st->token = t_digit;
    } else {
        switch (ch) {
        case '=':
            st->token = t_equal;
            read_char(st);
            break;
        case ';':
            st->token = t_semicolon;
            read_char(st);
            break;
        case '{':
            st->token = t_open_brace;
            read_char(st);
            break;
        case '}':
            st->token = t_close_brace;
            read_char(st);
            break;
        case '"':
        case '\'':
            st->token = t_string;
            st->quote = ch;
            break;
        }
    }
    if (st->token != t_string) {
        array_append(&st->token_s, &ch, 1);
    }
    return CHARON_OK;
}

int read_while(config_state_t* st, int ((*is_valid)(int)))
{
    int ch;
    while ((ch = read_char(st)) > 0) {
        if (!is_valid(ch)) {
            break;
        }
        // if (st->token == t_id && !isalnum(ch) && ch != '$' && ch != '_') {
        //     break;
        // }
        // if (st->token == t_digit && !isdigit(ch)) {
        //     break;
        // }
        array_append(&st->token_s, st->buf + st->pos - 1, 1);
    }
    if (ch < 0 && ch != -CHARON_EOF) {
        return ch;
    }
    array_append(&st->token_s, "\0", 1);
    return CHARON_OK;
}

int read_string(config_state_t* st)
{
    /* TODO: escape sequences in strings */
    int ch;
    array_clean(&st->token_s);
    while ((ch = read_char(st)) > 0 && (ch != st->quote)) {
        array_append(&st->token_s, st->buf + st->pos - 1, 1);
    }
    if (ch != st->quote) {
        return UNKNOWN_TOKEN;
    }
    read_char(st);
    array_append(&st->token_s, "\0", 1);
    return CHARON_OK;
}

static int is_id_ch(int ch)
{
    return isalnum(ch) || ch == '$' || ch == '_';
}

int read_token(config_state_t* st)
{
    int res;
    st->token = t_none;
    array_clean(&st->token_s);
    res = _determine_token_type(st);

    if (res < 0) {
        return res;
    }

    switch (st->token) {
    case t_id:
        res = read_while(st, is_id_ch);
        // charon_debug("t_id(\"%s\")", st->token_s.data);
        break;
    case t_digit:
        res = read_while(st, isdigit);
        // charon_debug("t_digit(%s)", st->token_s.data);
        break;
    case t_string:
        res = read_string(st);
        // charon_debug("t_string(\"%s\")", st->token_s.data);
        break;

    case t_none:
    case t_equal:
    case t_semicolon:
    case t_open_brace:
    case t_close_brace:
        array_append(&st->token_s, "\0", 1);
        // charon_debug("t_tok(\"%s\")", st->token_s.data);
        res = CHARON_OK;
        break;
    }
    return res;
}

static inline void copy_token_string(config_state_t* st, string_t* where)
{
    size_t sz = array_size(&st->token_s);
    where->start = copy_string(array_data(&st->token_s), sz);
    where->end = where->start + sz;
}

int config_set_current_section(config_state_t* st, char* name)
{
    conf_def_t* conf_def = st->defs;
    conf_section_def_t* sect_def;
    void* conf;

    st->current_section_def = NULL;

    while (CONF_DEF_IS_NOT_NULL(conf_def)) {
        sect_def = conf_def->sections;
        while (SECTION_DEF_IS_NOT_NULL(sect_def)) {
            if (!strcmp(sect_def->name, name)) {
                st->current_section_def = sect_def;
                conf = conf_def->conf;
                break;
            }
            sect_def++;
        }
        conf_def++;
    }

    if (st->current_section_def == NULL) {
        return -CHARON_ERR;
    }

    if (st->current_section_def->flags & CONF_ALLOW_MULTIPLE) {
        void** v = (void**)((char*)conf + st->current_section_def->offset);
        size_t idx = vector_size(v);
        __vector_resize(v, idx + 1, st->current_section_def->type_size);
        st->current_section = (char*)(*v) + st->current_section_def->type_size * idx;
    } else {
        st->current_section = (char*)conf + st->current_section_def->offset;
    }

    if (st->current_section_def->type_init != NULL) {
        st->current_section_def->type_init(st->current_section);
    }

    return 0;
}

conf_field_def_t* config_find_field_def(conf_section_def_t* section, char* name)
{
    conf_field_def_t* field_def = section->allowed_fields;
    while (FIELD_DEF_IS_NOT_NULL(field_def)) {
        if (!strcmp(field_def->name, name)) {
            return field_def;
        }
        field_def++;
    }
    return NULL;
}

int config_parse_string_field(config_state_t* st, conf_field_def_t* field_def)
{
    int res;
    string_t* str = (string_t*)((char*)st->current_section + field_def->offset);

    EXPECT_TOKEN(t_string);
    copy_token_string(st, str);
    EXPECT_TOKEN(t_semicolon);
    return CHARON_OK;
}

int config_parse_integer_field(config_state_t* st, conf_field_def_t* field_def)
{
    int res;
    int* val = (int*)((char*)st->current_section + field_def->offset);

    EXPECT_TOKEN(t_digit);
    *val = strtol(st->token_s.data, NULL, 0);
    EXPECT_TOKEN(t_semicolon);
    return CHARON_OK;
}

int config_parse_time_interval_field(config_state_t* st, conf_field_def_t* field_def)
{
    int res;
    time_t* val = (time_t*)((char*)st->current_section + field_def->offset);

    EXPECT_TOKEN(t_digit);
    time_t v = strtoll(st->token_s.data, NULL, 0);
    EXPECT_TOKEN(t_id);
    if (!strcasecmp(st->token_s.data, "s")) {
        *val = v;
    } else if (!strcasecmp(st->token_s.data, "m")) {
        *val = v * 60;
    } else if (!strcasecmp(st->token_s.data, "m")) {
        *val = v * 60 * 60;
    }
    EXPECT_TOKEN(t_semicolon);
    return CHARON_OK;
}

int config_parse_size_field(config_state_t* st, conf_field_def_t* field_def)
{
    int res;
    time_t* val = (time_t*)((char*)st->current_section + field_def->offset);

    EXPECT_TOKEN(t_digit);
    size_t v = strtoull(st->token_s.data, NULL, 0);
    EXPECT_TOKEN(t_id);
    if (!strcasecmp(st->token_s.data, "b")) {
        *val = v;
    } else if (!strcasecmp(st->token_s.data, "kb")) {
        *val = v * 1024;
    } else if (!strcasecmp(st->token_s.data, "mb")) {
        *val = v * 1024 * 1024;
    } else if (!strcasecmp(st->token_s.data, "gb")) {
        *val = v * 1024 * 1024 * 1024;
    } else {
        return -CHARON_ERR;
    }
    EXPECT_TOKEN(t_semicolon);
    return CHARON_OK;
}

int config_parse_field(config_state_t* st, conf_field_def_t* field_def)
{
    int res;
    switch (field_def->type) {
    case CONF_STRING:
        res = config_parse_string_field(st, field_def);
        break;
    case CONF_TIME_INTERVAL:
        res = config_parse_time_interval_field(st, field_def);
        break;
    case CONF_SIZE:
        res = config_parse_size_field(st, field_def);
        break;
    case CONF_INTEGER:
        res = config_parse_integer_field(st, field_def);
        break;
    default:
        return -CHARON_ERR;
    }
    if (res == CHARON_OK) {
        field_def->parsed = 1;
    }
    return res;
}

int config_parse_section(config_state_t* st)
{
    int res;
    EXPECT_TOKEN(t_open_brace);
    WHILE_TOKEN() {
        if (st->token == t_id) {
            conf_field_def_t* field_def = config_find_field_def(st->current_section_def, st->token_s.data);
            if (field_def == NULL) {
                charon_error("field '%s' is not allowed here", st->token_s.data);
                return -CHARON_ERR;
            }
            res = config_parse_field(st, field_def);
            if (res != CHARON_OK) {
                break;
            }
        } else {
            break;
        }
    }

    ASSERT_TOKEN(t_close_brace);
    st->current_section_def->parsed = 1;
    return res;
}

int config_check_required(config_state_t* st)
{
    conf_def_t* conf_def = st->defs;
    conf_field_def_t* field_def;
    conf_section_def_t* sect_def;

    while (CONF_DEF_IS_NOT_NULL(conf_def)) {
        sect_def = conf_def->sections;
        while (SECTION_DEF_IS_NOT_NULL(sect_def)) {
            if (!sect_def->parsed && (sect_def->flags & CONF_REQUIRED)) {
                charon_error("section '%s' is required", sect_def->name);
                return -CHARON_ERR;
            }

            if (sect_def->parsed) {
                field_def = sect_def->allowed_fields;
                while (FIELD_DEF_IS_NOT_NULL(field_def)) {
                    if (!field_def->parsed && (field_def->flags & CONF_REQUIRED)) {
                        charon_error("field '%s' in section '%s' is required", field_def->name, sect_def->name);
                        return -CHARON_ERR;
                    }
                    field_def++;
                }
            }

            sect_def++;
        }
        conf_def++;
    }
    return CHARON_OK;
}

int config_parse(config_state_t* st)
{
    int res = -CHARON_ERR;

    WHILE_TOKEN() {
        if (st->token == t_id) {
            if (config_set_current_section(st, st->token_s.data) != CHARON_OK) {
                charon_error("unknown section '%s'", st->token_s.data);
            }

            res = config_parse_section(st);
            if (res != CHARON_OK) {
                break;
            }
        } else {
            break;
        }
    }

    if (res != -CHARON_EOF) {
        charon_error("syntax error at %d:%d", st->line, st->line_pos);
        return -CHARON_ERR;
    }

    if (config_check_required(st) != CHARON_OK) {
        return -CHARON_ERR;
    }

    return CHARON_OK;
}

int config_open(char* filename, conf_def_t* defs)
{
    config_state_t st;
    int res = CHARON_OK;

    config_state_init(&st);
    st.fd = open(filename, O_RDONLY);
    st.defs = defs;
    if (st.fd < 0) {
        charon_perror("open: ");
        res = -CHARON_ERR;
        goto cleanup;
    }
    config_init(&st);
    res = config_parse(&st);

cleanup:
    close(st.fd);
    config_state_destroy(&st);
    return res;
}
