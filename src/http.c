#include "http.h"

void http_request_init(http_request_t* req)
{
    req->headers.content_length = 0;
}

void http_request_destroy(http_request_t* req)
{

}

void http_response_init(http_response_t* resp)
{
    resp->headers.content_length = 0;
}

void http_response_destroy(http_response_t* resp)
{

}

