#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Response Status Codes
//
// [1xx]          - wait for another reply
// [2xx]          - success
// [3xx]          - reserved
// [4xx]          - fail
// [5xx]          - hard fail 
// [6xx]          - reserved
// [7xx] to [9xx] - user extensions

#define LTP_RESPONSE_MAGIC "ltp0"
#define LTP_REQUEST_MAGIC "ltp1"

typedef uint32_t resp_status_code;

typedef struct {
    char magic[4];  // "LTP0"
    resp_status_code status_code;

    // body
    const uint8_t *body;
    size_t body_len;
} ltp_response;

// Request Methods
//
// PING  (ask if alive)
// GET   (read data)
// POST  (submit data)
// EXT   (ask for supported user extensions)
// ???   (anything else is a user extension)


// PING
//
// when a LTP server recives PING it should create a response with:
// status = 200 
// body = "PONG"
// (note: PING is path agnostic)

// EXT
//
// when an LTP server receives EXT it should create a response with:
// status = 200 
// body = newline delimited list of active server extensions or "None\n" if empty
// (note: EXT is path agnostic)


typedef struct {
    char magic[4];  // "LTP1"
    const char *method;
    size_t method_len;

    // path
    const char *path;
    size_t path_len;

    // body
    const uint8_t *body;
    size_t body_len;        
} ltp_request;

// ltplib stuff

typedef enum {
    LIBLTP_OK = 0,
    LIBLTP_ERR_INVALID_ARG,
    LIBLTP_ERR_BUFFER_TOO_SMALL,
    LIBLTP_ERR_INVALID_MAGIC,
    LIBLTP_ERR_MALFORMED
} ltp_error;

// helpers

static inline void ltp_set_response(ltp_response *resp, resp_status_code status, const char *body) {
    memcpy(resp->magic, LTP_RESPONSE_MAGIC, 4);
    resp->status_code = status;
    resp->body = (const uint8_t *)body;
    resp->body_len = body ? strlen(body) : 0;
}

static inline int ltp_method_is(const ltp_request *req, const char *method) {
    size_t len = strlen(method);
    return req->method_len == len && strncmp(req->method, method, len) == 0;
}

static inline int ltp_path_is(const ltp_request *req, const char *path) {
    size_t len = strlen(path);
    return req->path_len == len && strncmp(req->path, path, len) == 0;
}

// implementation

#ifdef LIBLTP_IMPLEMENTATION

// requests

ltp_error ltp_encode_request(const ltp_request *req, uint8_t *dest_buf, size_t max_len, size_t *out_len) {
    if (!req || !dest_buf || !out_len) {
        return LIBLTP_ERR_INVALID_ARG;
    }

    // get header size
    int header_len = snprintf(NULL, 0, "%.4s %.*s %.*s\n\n", req->magic, (int)req->method_len, req->method, (int)req->path_len, req->path);

    if (header_len < 0) {
        return LIBLTP_ERR_MALFORMED;
    }

    size_t total_len = (size_t)header_len + req->body_len;
    if (total_len > max_len) {
        return LIBLTP_ERR_BUFFER_TOO_SMALL;
    }

    int written = snprintf((char *)dest_buf, max_len, "%.4s %.*s %.*s\n\n", req->magic, (int)req->method_len, req->method, (int)req->path_len, req->path);
    if (written < 0 || written != header_len) {
        return LIBLTP_ERR_MALFORMED;
    }

    size_t cursor = (size_t)header_len;

    // copy body if exists
    if (req->body && req->body_len > 0) {
        memcpy(dest_buf + cursor, req->body, req->body_len);
        cursor += req->body_len;
    }

    *out_len = cursor;
    return LIBLTP_OK;
}

ltp_error ltp_decode_request(const uint8_t *src_buf, size_t len, ltp_request *req) {
    // minimum possible request size
    if (!src_buf || !req || len < 10) {
        return LIBLTP_ERR_INVALID_ARG;
    }

    // request magic check
    if (memcmp(src_buf, LTP_REQUEST_MAGIC, 4) != 0) {
        return LIBLTP_ERR_INVALID_MAGIC;
    }

    memcpy(req->magic, src_buf, 4);

    // make sure format structure spacing is correct
    if (src_buf[4] != ' ') {
        return LIBLTP_ERR_MALFORMED;
    }

    // find first space after method or space before path
    const uint8_t *method_start = &src_buf[5];
    const uint8_t *method_end = NULL;
    for (size_t i = 5; i < len - 1; i++) {
        if (src_buf[i] == ' ') {
            method_end = &src_buf[i];
            break;
        }
    }

    if (!method_end) {
        return LIBLTP_ERR_MALFORMED;
    }

    size_t method_len = (size_t)(method_end - method_start);
    if (method_len == 0) {
        return LIBLTP_ERR_MALFORMED;
    }

    req->method = (const char *)method_start;
    req->method_len = method_len;

    // find header separator "\n\n" starting after method_end
    const uint8_t *separator = NULL;
    for (size_t i = (size_t)(method_end - src_buf) + 1; i < len - 1; i++) {
        if (src_buf[i] == '\n' && src_buf[i + 1] == '\n') {
            separator = &src_buf[i];
            break;
        }
    }

    if (!separator) {
        return LIBLTP_ERR_MALFORMED;
    }

    // path is everything between method_end + 1 and separator
    const uint8_t *path_start = method_end + 1;
    size_t path_len = (size_t)(separator - path_start);

    if (path_len == 0) {
        return LIBLTP_ERR_MALFORMED;
    }

    req->path = (const char *)path_start;
    req->path_len = path_len;

    // body starts 2 bytes after separator "\n\n"
    const uint8_t *body_start = separator + 2;
    size_t parsed_len = (size_t)(body_start - src_buf);

    if (parsed_len < len) {
        req->body = body_start;
        req->body_len = len - parsed_len;
    } else {
        req->body = NULL;
        req->body_len = 0;
    }

    return LIBLTP_OK;
}

// responses

ltp_error ltp_encode_response(const ltp_response *resp, uint8_t *dest_buf, size_t max_len, size_t *out_len) {
    if (!resp || !dest_buf || !out_len) {
        return LIBLTP_ERR_INVALID_ARG;
    }

    // get header size
    int header_len = snprintf(NULL, 0, "%.4s %u\n\n", resp->magic, resp->status_code);

    if (header_len < 0) {
        return LIBLTP_ERR_MALFORMED;
    }

    size_t total_len = (size_t)header_len + resp->body_len;
    if (total_len > max_len) {
        return LIBLTP_ERR_BUFFER_TOO_SMALL;
    }

    int written = snprintf((char *)dest_buf, max_len, "%.4s %u\n\n", resp->magic, resp->status_code);
    if (written < 0 || written != header_len) {
        return LIBLTP_ERR_MALFORMED;
    }

    size_t cursor = (size_t)header_len;

    // copy body if exists
    if (resp->body && resp->body_len > 0) {
        memcpy(dest_buf + cursor, resp->body, resp->body_len);
        cursor += resp->body_len;
    }

    *out_len = cursor;
    return LIBLTP_OK;
}

ltp_error ltp_decode_response(const uint8_t *src_buf, size_t len, ltp_response *resp) {
    // minimum possible request size
    if (!src_buf || !resp || len < 8) {
        return LIBLTP_ERR_INVALID_ARG;
    }

    // response magic check
    if (memcmp(src_buf, LTP_RESPONSE_MAGIC, 4) != 0) {
        return LIBLTP_ERR_INVALID_MAGIC;
    }

    memcpy(resp->magic, src_buf, 4);

    // make sure format structure spacing is correct
    if (src_buf[4] != ' ') {
        return LIBLTP_ERR_MALFORMED;
    }

    // parse status code
    const char *status_str = (const char *)&src_buf[5];
    resp->status_code = (resp_status_code)strtoul(status_str, NULL, 10);

    // find header separator ("\n\n") using fast scan
    const uint8_t *separator = NULL;
    for (size_t i = 5; i < len - 1; i++) {
        if (src_buf[i] == '\n' && src_buf[i + 1] == '\n') {
            separator = &src_buf[i];
            break;
        }
    }

    if (!separator) {
        return LIBLTP_ERR_MALFORMED;
    }

    // body starts 2 bytes after separator (\n\n)
    const uint8_t *body_start = separator + 2;
    size_t parsed_len = (size_t)(body_start - src_buf);

    if (parsed_len < len) {
        resp->body = body_start;
        resp->body_len = len - parsed_len;
    } else {
        resp->body = NULL;
        resp->body_len = 0;
    }

    return LIBLTP_OK;
}

#endif // LIBLTP_IMPLEMENTATION
