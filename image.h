#ifndef TERMKCD_IMAGE_H
#define TERMKCD_IMAGE_H

// libpng
#include <png.h>

// libjpeg
#include <jpeglib.h>

enum file_ext {
    FILE_EXT_UNKNOWN,
    FILE_EXT_PNG,
    FILE_EXT_JPEG
};

void read_callback_png(png_structp png_ptr, png_bytep out_data, png_uint_32 size) {
    // Read the png file from memory
    // Notes:
    // - This was a pain in the ass to do as, contrary to what most online guides say,
    //   you DO NOT allocate memory for out_data, as it is allocated by libpng. Otherwise
    //   libpng reads undefined data, leading to failure.
    // - libpng DOES NOT keep track of the location for the next bytes if data to be
    //   read. This must be tracked by your custom I/O system.

    // Get custom IO struct pointer
    struct mem_block* io_ptr = png_get_io_ptr(png_ptr);
    if(io_ptr == NULL) {
        png_error(png_ptr, "png_get_io_ptr@read_callback_png: Could not retreive io_ptr!\n");
        return;
    }
    // Copy data from custom IO to out_data
    memcpy(out_data, io_ptr->ptr + io_ptr->i, size);
    // Update current char in custom IO
    io_ptr->i += size;
}

unsigned char* load_png(char* png_buf, size_t png_buf_len, png_uint_32* w, png_uint_32* h) {
    // Check PNG signature
    if(png_sig_cmp((png_bytep)png_buf, 0, 8)) {
        // Nothing initialized so no clean-up required, just exit
        fprintf(stderr, "png_sig_cmp@load_png: PNG signature invalid!\n");
        return NULL;
    }

    // Initialize data
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!png_ptr) {
        // Clean-up before erroneous exit
        fprintf(stderr, "png_create_read_struct@load_png: Could not create png read struct!\n");
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr) {
        // Clean-up before erroneous exit
        png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
        fprintf(stderr, "png_create_info_struct@load_png: Could not create png info struct!\n");
        return NULL;
    }

    if(setjmp(png_jmpbuf(png_ptr))) {
        // Clean-up before erroneous exit
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        fprintf(stderr, "@load_png: An error occured while trying to read the PNG file!\n");
        return NULL;
    }

    // Prepare custom IO and set read callback to use memory instead of file
    // Note that we are skipping the first 8 bytes since we already checked the png signature
    struct mem_block custom_io = {png_buf, 8};
    png_set_read_fn(png_ptr, &custom_io, read_callback_png);

    // Tell libpng we already checked the signature
    png_set_sig_bytes(png_ptr, 8);

    // Note: Low-ish level interface
    // Read png info
    png_read_info(png_ptr, info_ptr);

    // Retreive image info to variables
    int colour_type;
    int bit_depth;

    png_get_IHDR(png_ptr, info_ptr, w, h, &bit_depth, &colour_type, NULL, NULL, NULL);

    // Convert to correct bit depth
    if(bit_depth == 16)
        png_set_strip_16(png_ptr);
    else if(bit_depth < 8)
        png_set_packing(png_ptr);
    
    // Strip alpha
    png_set_strip_alpha(png_ptr);
    
    // Convert palette images to rgb
    if(colour_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    // Expand grayscale images to full 8 bits
    if(colour_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    // Convert grayscale images to rgb
    if(colour_type == PNG_COLOR_TYPE_GRAY || colour_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    // Use bgr instead of rgb
    png_set_bgr(png_ptr);

    // Update info
    png_read_update_info(png_ptr, info_ptr);

    // Allocate memory for rows
    size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    unsigned char* bmp_ptr = malloc((*h) * row_bytes);
    if(bmp_ptr == NULL)
        fprintf(stderr, "malloc@load_png: Out of memory!\n");
    else {
        // Read image (pixel data) row by row
        for(size_t n = 0; n < *h; ++n)
            png_read_row(png_ptr, (png_bytep)bmp_ptr + (n * row_bytes), NULL);
    }
    // Stop reading
    png_read_end(png_ptr, info_ptr);

    // Clean-up structs
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    return bmp_ptr;
}

struct jpeg_custom_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

void jpeg_custom_error_exit(j_common_ptr cinfo) {
    // Get error manager data
    struct jpeg_custom_error_mgr* err_mgr_ptr = (struct jpeg_custom_error_mgr*)cinfo->err;
    // Display error message
    (*cinfo->err->output_message)(cinfo);
    // Jump to the exit handler at load_jpeg
    longjmp(err_mgr_ptr->setjmp_buffer, 1);
}

unsigned char* load_jpeg(char* jpeg_buf, size_t jpeg_buf_len, long unsigned int* w, long unsigned int* h) {
    // Initialize data
    struct jpeg_decompress_struct cinfo;
    struct jpeg_custom_error_mgr error_mgr;
    // Get regular error routines, but update error_exit function with custom one
    cinfo.err = jpeg_std_error(&error_mgr.pub);
    error_mgr.pub.error_exit = jpeg_custom_error_exit;

    // Set-up jump point for load failure
    if(setjmp(error_mgr.setjmp_buffer)) {
        // Clean-up
        jpeg_destroy_decompress(&cinfo);
        // Return no data
        return NULL;
    }

    // Initialize JPEG decompression object, now that error handling is set up
    jpeg_create_decompress(&cinfo);

    // Specify data source. In this case, a memory source is used
    jpeg_mem_src(&cinfo, (unsigned char*)jpeg_buf, jpeg_buf_len);

    // Read file parameters
    jpeg_read_header(&cinfo, 1);

    // Set parameters for decompression
    // In this case, we are reading as BGR instead of RGB colour space
    cinfo.out_color_space = JCS_EXT_BGR;

    // Start decompressor
    jpeg_start_decompress(&cinfo);

    // Set up variables for decompression
    size_t row_stride = cinfo.output_width * cinfo.output_components;
    unsigned char* bmp_ptr = malloc(row_stride * cinfo.output_height);
    unsigned char* row_buf[1];

    // Read scanlines one by one
    while(cinfo.output_scanline < cinfo.output_height) {
        // Update bitmap pointer in scanline buffer
        row_buf[0] = bmp_ptr + cinfo.output_scanline * row_stride;
        // Read current scanline
        jpeg_read_scanlines(&cinfo, row_buf, 1);
    }

    // Update width and height variables
    (*w) = cinfo.output_width;
    (*h) = cinfo.output_height;

    // Finish decompressor
    jpeg_finish_decompress(&cinfo);

    // Clean-up
    jpeg_destroy_decompress(&cinfo);
    return bmp_ptr;
}

enum file_ext get_extension(struct mem_block* str) {
    // Gets which file extension the string has
    // 0: Unknown: !Png & !Jpeg
    // 1: Errored, due to being out of memory
    // 2: Png    : png
    // 3: Jpeg   : jpg, jpeg (and jpe, jif, jfif, jfi according to wikipedia)
    // Note that, because data is read from ending to beginning, the comparison strings will be reversed.
    // E.g: to test for png extension, ext_buf is compared with gnp

    char ext_buf[str->i + 1]; // Allocate to the size of the address, as it is the maximum possible, and +1 for null

    for(size_t n = 0; n < str->i; ++n) { // Iterated from 0 to max, but this represents end to begin in str
        if(str->ptr[str->i - n - 1] == '.') { // Last dot indicates file extension beginning
            ext_buf[n] = '\0'; // Null terminate to make it a valid string for comparison
            if(strcmp(ext_buf, "gnp") == 0) // .png?
                return FILE_EXT_PNG; // PNG
            else if(strcmp(ext_buf, "gpj") == 0) // .jpg?
                return FILE_EXT_JPEG; // JPEG
            else if(strcmp(ext_buf, "gepj") == 0) // .jpeg?
                return FILE_EXT_JPEG; // JPEG
            else if(strcmp(ext_buf, "epj") == 0) // .jpe?
                return FILE_EXT_JPEG; // JPEG
            else if(strcmp(ext_buf, "fij") == 0) // .jif?
                return FILE_EXT_JPEG; // JPEG
            else if(strcmp(ext_buf, "fifj") == 0) // .jfif?
                return FILE_EXT_JPEG; // JPEG
            else if(strcmp(ext_buf, "ifj") == 0) // .jfi?
                return FILE_EXT_JPEG; // JPEG
            else
                return FILE_EXT_UNKNOWN;
        }
        else
            ext_buf[n] = str->ptr[str->i - n - 1];
    }
    return FILE_EXT_UNKNOWN; // Never even reached a dot, no file extension
}

#endif
