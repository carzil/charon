#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

#include "conf.h"
#include "defs.h"
#include "utils/logging.h"
#include "utils/array.h"

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
        st_server_start,
    } state;

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

    config_t* conf;

} config_state_t;

void config_state_init(config_state_t* state)
{
    state->pos = 0;
    state->eof = 0;
    state->size = 0;
    state->state = st_server_start;
    state->line = 1;
    state->line_pos = 0;
    array_init(&state->token_s, 10);
}

void config_state_destroy(config_state_t* st)
{
    array_destroy(&st->token_s);
}

void config_init(config_t* conf)
{
    LIST_HEAD_INIT(conf->vhosts);
}

config_t* config_create()
{
    config_t* conf = malloc(sizeof(config_t));
    config_init(conf);
    return conf;
}

void config_destroy(config_t* conf)
{
    struct list_node* ptr;
    struct list_node* tmp;
    list_foreach_safe(&conf->vhosts, ptr, tmp) {
        vhost_t* p = list_entry(ptr, vhost_t, lnode);
        vhost_destroy(p);
        free(p);
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

    while ((ch = read_char(st)) > 0 && isspace(ch));

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
            break;
        case ';':
            st->token = t_semicolon;
            break;
        case '{':
            st->token = t_open_brace;
            break;
        case '}':
            st->token = t_close_brace;
            break;
        case '"':
        case '\'':
            st->token = t_string;
            st->quote = ch;
            break;
        }
    }
    if (st->token != t_string) {
        array_append(&st->token_s, st->buf + st->pos - 1, 1);
    }
    return CHARON_OK;
}

int read_while(config_state_t* st)
{
    int ch;
    while ((ch = read_char(st)) > 0) {
        if (st->token == t_id && !isalnum(ch)) {
            break;
        }
        if (st->token == t_digit && !isdigit(ch)) {
            break;
        }
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
    array_append(&st->token_s, "\0", 1);
    return CHARON_OK;
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
        res = read_while(st);
        // charon_debug("t_id(\"%s\")", st->token_s.data);
        break;
    case t_digit:
        res = read_while(st);
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

int config_parse_vhost_field(config_state_t* st, vhost_t* vhost)
{
    int res;
    if (!strcmp(array_data(&st->token_s), "name")) {
        EXPECT_TOKEN(t_string);
        size_t sz = array_size(&st->token_s);
        vhost->name.start = copy_string(array_data(&st->token_s), sz);
        EXPECT_TOKEN(t_semicolon);
    }
    return CHARON_OK;
}

int config_parse_vhost(config_state_t* st, vhost_t** vhost)
{
    int res;
    EXPECT_TOKEN(t_open_brace);

    *vhost = vhost_create();

    WHILE_TOKEN() {
        switch (st->token) {
        case t_id:
            res = config_parse_vhost_field(st, *vhost);
            break;
        default:
            goto end;
        }
    }

end:
    ASSERT_TOKEN_F(t_close_brace, err);
    return CHARON_OK;

err:
    vhost_destroy(*vhost);
    *vhost = NULL;
    return res;
}

int config_parse(config_state_t* st)
{
    int res;

    WHILE_TOKEN() {
        if (st->token == t_id && !strcmp(st->token_s.data, "vhost")) {
            vhost_t* vhost;
            res = config_parse_vhost(st, &vhost);
            if (res != CHARON_OK) {
                break;
            }
            list_insert_last(&st->conf->vhosts, &vhost->lnode);
        }
    }
    if (res != -CHARON_EOF) {
        charon_error("syntax error at %d:%d", st->line, st->line_pos);
        return -CHARON_ERR;
    }
    return CHARON_OK;
}

int config_open(char* filename, config_t** conf)
{
    config_state_t st;
    int res = CHARON_OK;

    config_state_init(&st);
    st.fd = open(filename, O_RDONLY);
    if (st.fd < 0) {
        charon_perror("open: ");
        res = -CHARON_ERR;
        goto cleanup;
    }

    *conf = st.conf = config_create();
    res = config_parse(&st);

cleanup:
    close(st.fd);
    config_state_destroy(&st);
    return res;
}
