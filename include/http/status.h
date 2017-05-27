#ifndef _CHARON_STATUS_H_
#define _CHARON_STATUS_H_

#include "utils/string.h"

const string_t HTTP_STATUSES[5][100];
#define http_get_status_message(code) (HTTP_STATUSES[code / 100 - 1][code % 100])
#define http_status_valid(code) (100 <= code && code <= 511 && HTTP_STATUSES[code / 100 - 1][code % 100].start != NULL)

#endif
