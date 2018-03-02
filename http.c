/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "http.h"
#include "error_stack.h"

#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

enum { this_error_context =fdu_context_http };

//

fdu_http_request_parser_t *fdu_new_http_request_parser(void *context, const fdu_http_spec_t *http_spec)
{
    fdu_http_request_parser_t *parser =malloc(sizeof(fdu_http_request_parser_t));

    if (!parser)
        return 0;

    parser->context =context;

    parser->parser_state.progress           =fdu_http_progress_request_line;
    parser->parser_state.content_loaded     =0;

    parser->message_state.method            =0;
    parser->message_state.version           =0;
    parser->message_state.closing           =true;
    parser->message_state.content_length    =0;
    parser->message_state.content_type[0]   =0;

    parser->http_spec =http_spec;

    return parser;
}

void fdu_free_http_request_parser(fdu_http_request_parser_t *parser)
{
    free(parser);
}

// *********************************************************

static void convert_to_lowercase(unsigned char *ptr, const unsigned char *const end)
{
    for (; ptr < end; ++ptr)
    {
        if (*ptr < 'A') continue;
        else if (*ptr <= 'Z') *ptr += 'a' - 'A';
        else if (*ptr < 192
                 || *ptr == 215)
        {
            continue;
        }
        else if (*ptr <= 222) *ptr += 224-192;
    }
}

static void strip_ws_start(unsigned char **start, const unsigned char *const end)
{
    while (*start < end
           && isspace(**start))
    {
        ++*start;
    }
}

static void strip_ws_end(const unsigned char *const start, unsigned char **end)
{
    while (start < *end
           && isspace(*(*end-1)))
    {
        --*end;
    }
}

static void strip_ws_both(unsigned char **start, unsigned char **end)
{
    strip_ws_end(*start, end);
    strip_ws_start(start, *end);
}

// *********************************************************

static bool fdu_http_parse_header(fdu_http_request_parser_t *parser,
                                  unsigned char *startOfName,
                                  unsigned char *endOfContent)
{
    unsigned char *endOfName =memchr(startOfName, ':', endOfContent-startOfName);
    if (!endOfName) {
        fde_push_http_error("Corrupted header line", 400);
        return false;
    }

    unsigned char *startOfContent =endOfName+1;

    strip_ws_both(&startOfName, &endOfName);

    if (startOfName >= endOfName) {
        fde_push_http_error("Corrupted header line", 400);
        return false;
    }

    *endOfName =0;
    convert_to_lowercase(startOfName, endOfName);

    strip_ws_both(&startOfContent, &endOfContent);
    *endOfContent =0;

    switch (startOfName[0]) {
    case 'c':
        if (strcmp((const char *)startOfName, "connection") == 0)
        {
            convert_to_lowercase(startOfContent, endOfContent);

            if (parser->message_state.version == fdu_http_version_1_0) {
                if (strcmp((const char *)startOfContent, "keep-alive") == 0)
                    parser->message_state.closing =false;
            }
            else if (parser->message_state.version == fdu_http_version_1_1) {
                if (strcmp((const char *)startOfContent, "close") == 0)
                    parser->message_state.closing =true;
            }
        }
        else if (strcmp((const char *)startOfName, "content-length") == 0)
        {
            enum { TmpBufferSize = 20 };

            const int input_size =endOfContent-startOfContent;

            if (input_size <= 0
                || input_size >= TmpBufferSize)
            {
                fde_push_http_error("Corrupted \"Content-length\" header", 400);
                return false;
            }

            for (int i =0; i<input_size; ++i)
            {
                if (!isdigit(startOfContent[input_size])) {
                    fde_push_http_error("Corrupted \"Content-length\" header", 400);
                    return false;
                }
            }

            char cl_tmp_buffer[TmpBufferSize];

            memcpy(cl_tmp_buffer, startOfContent, input_size);
            cl_tmp_buffer[input_size] =0;

            errno =0;
            char *endptr;
            const uint32_t cl =strtoul(cl_tmp_buffer, &endptr, 10);

            if (errno
                || endptr != &cl_tmp_buffer[input_size]
                || *endptr != 0)
            {
                fde_push_http_error("Corrupted \"Content-length\" header", 400);
                return false;
            }

            parser->message_state.content_length =cl;
        }
        else if (strcmp((const char *)startOfName, "content-type") == 0)
        {
            if (endOfContent-startOfContent < 1)
                break;

            unsigned int ct_size =endOfContent-startOfContent;
            if (ct_size >= ContentTypeSize)
                ct_size =ContentTypeSize-1;

            memcpy(parser->message_state.content_type, startOfContent, ct_size);
        }
        break;
    }

    if (parser->http_spec->parse_header
        && !parser->http_spec->parse_header(parser->context, startOfName, startOfContent))
    {
        return false;
    }

    return true;
}

// *********************************************************

bool fdu_http_parse_request(fdu_http_request_parser_t *parser,
                            unsigned char **start,
                            unsigned char **end)
{
    const fde_node_t *ectx =0;

    if (!(ectx =fde_push_context(this_error_context)))
        return false;
    //

    unsigned char *sol, *eol;   // start/end of line

    switch (parser->parser_state.progress) {
    case fdu_http_progress_request_line:
        if (!(eol =memchr(*start, '\n', *end - *start)))
            break;
        if (eol - *start < 6) { // min length
            fde_push_http_error("Too short request line", 400);
            return false;
        }

        sol =*start;
        *start =eol+1;
        if (*(eol-1) == '\r')   // was CRLF ?
            --eol;

        {
            unsigned char *first_space, *second_space;

            if (!(first_space =memchr(sol, ' ', eol-sol))
                || !(second_space =memchr(first_space+1, ' ', eol-(first_space+1)))
                || memchr(second_space+1, ' ', eol-(second_space+1)))
            {
                fde_push_http_error("Corrupted request line", 400);
                return false;
            }

            // method

            if (first_space - sol == 3
                && memcmp(sol, "GET", 3) == 0)
            {
                parser->message_state.method =fdu_http_method_get;
            }
            else if (first_space - sol == 4
                     && memcmp(sol, "HEAD", 4) == 0)
            {
                parser->message_state.method =fdu_http_method_head;
            }
            else if (first_space - sol == 4
                     && memcmp(sol, "POST", 4) == 0)
            {
                parser->message_state.method =fdu_http_method_post;
            }
            else {
                fde_push_http_error("Method not implemented", 501);
                return false;
            }

            if (parser->http_spec->parse_url
                && !parser->http_spec->parse_url(parser->context,
                                                 parser->message_state.method,
                                                 first_space+1,
                                                 second_space))
            {
                return false;
            }

            if (eol - (second_space+1) != 8) {
                fde_push_http_error("Corrupted HTTP version", 400);
                return false;
            }

            if (memcmp(second_space+1, "HTTP/1.0", 8) == 0)
            {
                parser->message_state.version =fdu_http_version_1_0;

                if (parser->http_spec->parse_version
                    && !parser->http_spec->parse_version(parser->context, fdu_http_version_1_0))
                {
                    return false;
                }

                parser->message_state.closing =true;
            }
            else if (memcmp(second_space+1, "HTTP/1.1", 8) == 0)
            {
                parser->message_state.version =fdu_http_version_1_1;

                if (parser->http_spec->parse_version
                    && !parser->http_spec->parse_version(parser->context, fdu_http_version_1_1))
                {
                    return false;
                }

                parser->message_state.closing =false;
            }
            else {
                fde_push_http_error("HTTP version not supported", 505);
                return false;
            }
        }

        parser->parser_state.progress =fdu_http_progress_headers;
        // fallthrough

    case fdu_http_progress_headers:
        // *start:      start of header
        // *eol:        end of header
        // sol:         where to start looking for the end of header
        // next_header: start of the next header
        sol =*start;
        while ((eol =memchr(sol, '\n', *end - sol)))
        {
            unsigned char *next_header =eol+1;

            if (eol > *start && *(eol-1) == '\r')       // CRLF
                --eol;

            if (*start == eol) {        // empty line
                *start =next_header;
                parser->parser_state.progress =fdu_http_progress_content_not_read;
                break;
            }

            if (next_header >= *end)
                break;

            if (*next_header == ' ' || *next_header == '\t')    // header continues
            {
                *eol++ =' ';
                do ++next_header;
                while (next_header < *end
                       && (*next_header == ' ' || *next_header == '\t'));
                //
                memmove(eol, next_header, *end - next_header);
                *end -=next_header - eol;
                //
                sol =eol;
                continue;
            }

            if (!fdu_http_parse_header(parser, *start, eol))
                return false;

            //
            sol =*start =next_header;
        }

        if (parser->parser_state.progress != fdu_http_progress_content_not_read)
            break;
        //fallthrough

    case fdu_http_progress_content_not_read:
        if (!parser->message_state.content_length) {
            parser->parser_state.progress =fdu_http_progress_done;
        }
        else if (parser->message_state.content_length > MaxContentLength) {
            fde_push_http_error("Too long content", 413);
            return false;
        }
        else if ((uint32_t)(*end - *start) >= parser->message_state.content_length)
            parser->parser_state.progress =fdu_http_progress_reading_content;

        if (parser->parser_state.progress != fdu_http_progress_reading_content)
            break;
        //fallthrough

    case fdu_http_progress_reading_content:
        {


#if 1


            /* Let's just dump all the content to
             * parser->http_spec->parse_content() for now. We need to get things
             * moving in other areas, then worry about POST-parameters.
             */

            uint32_t bytes_missing =parser->message_state.content_length - parser->parser_state.content_loaded;

            if (bytes_missing > (uint32_t)(*end - *start))
                bytes_missing = *end - *start;

            if (parser->http_spec->parse_content
                && !parser->http_spec->parse_content(parser->context, *start, *start + bytes_missing))
            {
                return false;
            }

            *start +=bytes_missing;
            parser->parser_state.content_loaded +=bytes_missing;

            if (parser->parser_state.content_loaded == parser->message_state.content_length)
                parser->parser_state.progress =fdu_http_progress_done;


#else


            const bool parsing =(parser->message_state.method == fdu_http_method_post
                                 && strcmp((const char *)parser->message_state.content_type,
                                           "application/x-www-form-urlencoded") == 0);

            if (parsing) {

            }
            else {

            }
#endif
        }

        if (parser->parser_state.progress != fdu_http_progress_done)
            break;
        //fallthrough

    case fdu_http_progress_done:
        return fde_safe_pop_context(this_error_context, ectx);
    }

    fde_push_resource_failure_id(fde_resource_buffer_underflow);
    return false;
}

// *********************************************************

static const char *get_http_error_message(uint32_t code)
{
    switch (code) {
    case 100: return "Continue";
    case 101: return "Swithching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choises";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Time-out";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Time-out";
    case 505: return "HTTP Version Not Supported";
    default: return 0;
    }
}

static const char *get_http_date(void)
{
    // "Sun, 06 Nov 1994 08:49:37 GMT"

    static const char *WeekDays[] ={
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
    };

    static const char *Months[] ={
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    //

    time_t now =time(0);

    struct tm tm;
    gmtime_r(&now, &tm);

    //

    enum { date_buffer_size =30 };

    static char date_buffer[date_buffer_size];
    char format_buffer[date_buffer_size];

#ifdef FD_DEBUG
    const int bytes_written1 =
#endif
        snprintf(format_buffer, date_buffer_size,
                 "%s, %%d %s %%Y %%T GMT",
                 WeekDays[tm.tm_wday%7],
                 Months[tm.tm_mon%12]);
    format_buffer[date_buffer_size -1] =0;

#ifdef FD_DEBUG
    if (bytes_written1 >= date_buffer_size)
        abort();
#endif

    const int bytes_written2 =strftime(date_buffer, date_buffer_size, format_buffer, &tm);
    date_buffer[date_buffer_size-1] =0;

#ifdef FD_DEBUG
    if (bytes_written2 != 29)
        abort();
#endif

    return (bytes_written2 == 29) ? date_buffer : 0;
}

// *********************************************************

const char *fdu_http_default_error_message =
#ifdef FD_DEBUG
    "(This is the default error response string, which should be replaced by code-specific message!)\n"
#else
    ""
#endif
    ;

static bool apply_data_to_buffer(const char *data,
                                 unsigned int length,
                                 unsigned char **startp,
                                 const unsigned char *end)
{
    if (*startp > end
        || (unsigned long)(end - *startp) < length)
    {
        return false;
    }

    if (length) {
        memcpy(*startp, data, length);
        *startp +=length;
    }

    return true;
}

bool fdu_http_conjure_error_response(fdu_http_request_parser_t *parser,
                                     unsigned int error_code,
                                     const char *content,
                                     unsigned char **startp,
                                     const unsigned char *end)
{
    const fde_node_t *ectx =0;

    if (!(ectx =fde_push_context(this_error_context)))
        return false;

    if (!parser
        || error_code < 100
        || error_code >= 600
        || !startp
        || !end)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    const char *error_message =get_http_error_message(error_code);

    if (!error_message) {
        fde_push_message("invalid error code");
        return false;
    }

    if (content == fdu_http_default_error_message)
        content =error_message;

    const uint32_t content_size =(content ? strlen(content) : 0);

    // conjure response

    {
        const int response_and_date_length
            =snprintf((char *)*startp, end - *startp,
                      "%s %u %s\r\n"
                      "Date: %s\r\n"
                      "Content-length: %u\r\n",
                      parser->message_state.version == fdu_http_version_1_1 ? "HTTP/1.1" : "HTTP/1.0",
                      error_code,
                      error_message,
                      get_http_date(),
                      content_size);

        if (*startp + response_and_date_length > end) {
            fde_push_resource_failure_id(fde_resource_buffer_overflow);
            return false;
        }

        *startp +=response_and_date_length;
    }

    if ((content_size
         && !apply_data_to_buffer("Content-type: text/plain\r\n", 26, startp, end))
        || (parser->message_state.closing
            && !apply_data_to_buffer("Connection: close\r\n", 19, startp, end))
        || (!parser->message_state.closing
            && parser->message_state.version == fdu_http_version_1_0
            && !apply_data_to_buffer("Connection: Keep-Alive\r\n", 24, startp, end)))
    {
        fde_push_resource_failure_id(fde_resource_buffer_overflow);
        return false;
    }

    return fde_safe_pop_context(this_error_context, ectx);
}
