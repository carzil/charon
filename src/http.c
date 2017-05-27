#include "http.h"

const string_t HTTP_STATUSES[5][100] = {
    /* 1xx */
    [0] = {
        [0] = string("Continue"),
        [1] = string("Switching Protocols"),
        [2] = string("Processing"),
    },

    /* 2xx */
    [1] = {
        [0] = string("OK"),
        [1] = string("Created"),
        [2] = string("Accepted"),
        [3] = string("Non-Authoritative Information"),
        [4] = string("No Content"),
        [5] = string("Reset Content"),
        [6] = string("Partial Content"),
        [7] = string("Multi-Status"),
        [8] = string("Already Reported"),
        [26] = string("IM Used"),
    },

    /* 3xx */
    [2] = {
        [0] = string("Multiple Choices"),
        [1] = string("Moved Permanently"),
        [2] = string("Found"),
        [3] = string("See Other"),
        [4] = string("Not Modified"),
        [5] = string("Use Proxy"),
        [6] = string("Switch Proxy"),
        [7] = string("Temporary Redirect"),
        [8] = string("Permanent Redirect"),
    },

    /* 4xx */
    [3] = {
        [0] = string("Bad Request"),
        [1] = string("Unauthorized"),
        [2] = string("Payment Required"),
        [3] = string("Forbidden"),
        [4] = string("Not Found"),
        [5] = string("Method Not Allowed"),
        [6] = string("Not Acceptable"),
        [7] = string("Proxy Authentication Required"),
        [8] = string("Request Timeout"),
        [9] = string("Conflict"),
        [10] = string("Gone"),
        [11] = string("Length Required"),
        [12] = string("Precondition Failed"),
        [13] = string("Payload Too Large"),
        [14] = string("URI Too Long"),
        [15] = string("Unsupported Media Type"),
        [16] = string("Range Not Satisfiable"),
        [17] = string("Expectation Failed"),
        [18] = string("I'm a teapot"),
        [21] = string("Misdirected Request"),
        [22] = string("Unprocessable Entity"),
        [23] = string("Locked"),
        [24] = string("Failed Dependency"),
        [26] = string("Upgrade Required"),
        [28] = string("Precondition Required"),
        [29] = string("Too Many Requests"),
        [31] = string("Request Header Fields Too Large"),
        [51] = string("Unavailable For Legal Reasons"),
    },

    /* 5xx */
    [4] = {
        [0] = string("Internal Server Error"),
        [1] = string("Not Implemented"),
        [2] = string("Bad Gateway"),
        [3] = string("Service Unavailable"),
        [4] = string("Gateway Timeout"),
        [5] = string("HTTP Version Not Supported"),
        [6] = string("Variant Also Negotiates"),
        [7] = string("Insufficient Storage"),
        [8] = string("Loop Detected"),
        [10] = string("Not Extended"),
        [11] = string("Network Authentication Required"),
    }
};

int write_header_s(string_t name, string_t value, buffer_t* buf)
{
    memcpy(buf->last, name.start, string_size(&name));
    buf->last += string_size(&name);
    *buf->last++ = ':';
    *buf->last++ = ' ';
    memcpy(buf->last, value.start, string_size(&value));
    buf->last += string_size(&value);
    *buf->last++ = '\r';
    *buf->last++ = '\n';

    return CHARON_OK;
}

int write_header_i(string_t name, int value, buffer_t* buf)
{

    memcpy(buf->last, name.start, string_size(&name));
    buf->last += string_size(&name);
    *buf->last++ = ':';
    *buf->last++ = ' ';
    buf->last += snprintf(buf->last, buf->end - buf->last, "%d", value);
    *buf->last++ = '\r';
    *buf->last++ = '\n';

    return CHARON_OK;
}

int http_write_status_line(buffer_t* buf, http_version_t version, http_status_t status, string_t status_message)
{
    memcpy(buf->last, "HTTP/", 5);
    buf->last += 5;
    buf->last += snprintf(buf->last, buf->end - buf->last, "%d.%d %d ",
        version.major,
        version.minor,
        status
    );
    memcpy(buf->last, status_message.start, string_size(&status_message));
    buf->last += string_size(&status_message);
    *buf->last++ = '\r';
    *buf->last++ = '\n';
    return CHARON_OK;
}
