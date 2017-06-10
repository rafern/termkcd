#ifndef TERMKCD_WEB_H
#define TERMKCD_WEB_H

// curl
#include <curl/curl.h>

struct json_parsed {
    struct mem_block month;
    struct mem_block num;
    struct mem_block link;
    struct mem_block year;
    struct mem_block news;
    struct mem_block safe_title;
    struct mem_block transcript;
    struct mem_block alt;
    struct mem_block img;
    struct mem_block title;
    struct mem_block day;
};

size_t write_callback_curl(char* buf, size_t size, size_t nmemb, struct mem_block* mem) {
    mem->ptr = memapp(buf, size * nmemb, mem->ptr, mem->i, 0);
    if(mem->ptr == NULL) {
        mem->i = 0;
        return (size * nmemb) + 1; // Anything != size * nmemb will tell curl that an error has occured. size * nmemb may be 0, so return (size * nmemb) + 1 instead of just 0
    }
    else {
        mem->i += size * nmemb;
        return size * nmemb;
    }
}

int parse_json(struct mem_block* raw, struct json_parsed* parsed, int debug) {
    size_t buf_n = 0; // Current pos in buffer
    size_t buf_len = 32; // Current buffer length (starts with 32 bytes, doubles when needed)
    char* buffer = malloc(buf_len); // Dynamic buffer for storing current thing to parse

    if(buffer == NULL) {
        fprintf(stderr, "malloc@parse_json: Out of memory!\n");
        return 0;
    }

    parsed->month      = empty_mem;
    parsed->num        = empty_mem;
    parsed->link       = empty_mem;
    parsed->year       = empty_mem;
    parsed->news       = empty_mem;
    parsed->safe_title = empty_mem;
    parsed->transcript = empty_mem;
    parsed->alt        = empty_mem;
    parsed->img        = empty_mem;
    parsed->title      = empty_mem;
    parsed->day        = empty_mem;
    struct mem_block cur_key = empty_mem;

    // Booleans:
    // 0: Inside quote
    // 1: Escape next char
    // 2: Key/value mode (0 = key, 1 = value)
    char modes = 0;
    for(size_t n = 1; n < raw->i; ++n) { // Start after { so it ignores the first table indicator.
        if(get_bit(modes, 0)) { // Inside quote
            if(get_bit(modes, 1)) { // Next char escaped
                if(raw->ptr[n] == 'n')
                    buffer[buf_n++] = '\n';
                else if(raw->ptr[n] == 'b')
                    buffer[buf_n++] = '\b';
                else if(raw->ptr[n] == 'f')
                    buffer[buf_n++] = '\f';
                else if(raw->ptr[n] == 'r')
                    buffer[buf_n++] = '\r';
                else if(raw->ptr[n] == 't')
                    buffer[buf_n++] = '\t';
                else
                    buffer[buf_n++] = raw->ptr[n];
                set_bit(&modes, 1, 0);
            }
            else { // Next char not escaped
                if(raw->ptr[n] == '"')
                    set_bit(&modes, 0, 0);
                else if(raw->ptr[n] == '\\')
                    set_bit(&modes, 1, 1);
                else
                    buffer[buf_n++] = raw->ptr[n];
            }
        }
        else { // Outside quote
            if(raw->ptr[n] == '"')
                set_bit(&modes, 0, 1);
            else if(raw->ptr[n] == ':') {
                set_bit(&modes, 2, 1);
                buffer[buf_n++] = '\0';
                if(!set_string(&cur_key, buffer, buf_n)) {
                    free(buffer);
                    return 0;
                }
                buf_n = 0;
            }
            else if(raw->ptr[n] == ',' || raw->ptr[n] == '}') {
                set_bit(&modes, 2, 0);
                buffer[buf_n++] = '\0';

                struct mem_block* cur_mem_ptr = NULL;
                if(strcmp(cur_key.ptr, "month") == 0)
                    cur_mem_ptr = &parsed->month;
                else if(strcmp(cur_key.ptr, "num") == 0)
                    cur_mem_ptr = &parsed->num;
                else if(strcmp(cur_key.ptr, "link") == 0)
                    cur_mem_ptr = &parsed->link;
                else if(strcmp(cur_key.ptr, "year") == 0)
                    cur_mem_ptr = &parsed->year;
                else if(strcmp(cur_key.ptr, "news") == 0)
                    cur_mem_ptr = &parsed->news;
                else if(strcmp(cur_key.ptr, "safe_title") == 0)
                    cur_mem_ptr = &parsed->safe_title;
                else if(strcmp(cur_key.ptr, "transcript") == 0)
                    cur_mem_ptr = &parsed->transcript;
                else if(strcmp(cur_key.ptr, "alt") == 0)
                    cur_mem_ptr = &parsed->alt;
                else if(strcmp(cur_key.ptr, "img") == 0)
                    cur_mem_ptr = &parsed->img;
                else if(strcmp(cur_key.ptr, "title") == 0)
                    cur_mem_ptr = &parsed->title;
                else if(strcmp(cur_key.ptr, "day") == 0)
                    cur_mem_ptr = &parsed->day;
                if(cur_mem_ptr != NULL) {
                    if(!set_string(cur_mem_ptr, buffer, buf_n)) {
                        free(cur_key.ptr);
                        free(buffer);
                        return 0;
                    }
                }
                else if(debug)
                    fprintf(stderr, "@parse_json: Unknown json key (%s)! Ignoring\n", buffer);
                buf_n = 0;
            }
            else if(raw->ptr[n] >= '0' && raw->ptr[n] <= '9')
                buffer[buf_n++] = raw->ptr[n];
            else if(raw->ptr[n] != ' ' && debug)
                fprintf(stderr, "@parse_json: Unknown char (%c)! Ignoring\n", raw->ptr[n]);
        }

        if(buf_n == (buf_len - 1)) { // Expand buffer by x2 if it reaches limit length
            buf_len *= 2;
            buffer = realloc(buffer, buf_len);
            if(buffer == NULL) {
                fprintf(stderr, "realloc@parse_json: Out of memory!\n");
                free(cur_key.ptr);
                return 0;
            }
        }
    }
    free(cur_key.ptr);
    free(buffer);
    return 1;
}

#endif
