#ifndef TERMKCD_MEMORY_H
#define TERMKCD_MEMORY_H

// Include string header
#include <string.h>

struct mem_block {
    char* ptr;
    size_t i;
} empty_mem = {NULL, 0};

char* memapp(void* src, size_t src_size, void* dest, size_t dest_offset, size_t dest_padding) {
    if(dest == NULL) // First time allocating memory: malloc
        dest = malloc(dest_offset + src_size + dest_padding);
    else // Not the first time allocating memory: realloc
        dest = realloc(dest, dest_offset + src_size + dest_padding);
    if(dest == NULL) {
        fprintf(stderr, "malloc/realloc@memapp: Out of memory!\n");
        return NULL;
    }
    memcpy((unsigned char*)dest + dest_offset, src, src_size);
    return dest;
}

void stride_memset(void* dest, unsigned char val, size_t val_n, size_t elem_n, size_t stride) {
    for(size_t i = 0; i < elem_n; ++i)
        memset((unsigned char*)dest + (i * stride), val, val_n);
}

void stride_memcpy(void* dest, void* src, size_t src_elem_n, size_t stride, size_t elem_size) {
    for(size_t i = 0; i < src_elem_n; ++i)
        memcpy((unsigned char*)dest + (i * stride), (unsigned char*)src + (i * elem_size), elem_size);
}

int set_string(struct mem_block* str, char* data, size_t len) {
    str->ptr = memapp(data, len, str->ptr, 0, 1);
    if(str->ptr == NULL) {
        str->i = 0;
        return 0;
    }
    else {
        str->i = len - 1;
        return 1;
    }
}

int get_bit(char bitmap, char n) {
    return (bitmap >> n) & 1;
}

void set_bit(char* bitmap, char n, char b) {
    (*bitmap) ^= (-b ^ (*bitmap)) & (1 << n);
}

#endif
