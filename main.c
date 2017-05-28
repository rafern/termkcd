// General includes
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

// curl
#include <curl/curl.h>

// libpng & zlib
#include <zlib.h>
#include <png.h>

// TODO: libjpeg
//#include <jpeglib.h>

// Includes for writing to framebuffer
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <unistd.h>
#include <fcntl.h>

// Includes for raw input
#include <termios.h>

// TODO:
// * make framebuffer viewer text independent of terminal text to remove uglyness (use memory fonts)
// * add jpeg support (only used in comics 1 to 119)

struct mem_block {
    char* ptr;
    size_t i;
} empty_mem = {NULL, 0};

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

char* memapp(char* src, size_t src_size, char* dest, size_t dest_offset, size_t dest_padding) {
    if(dest == NULL) // First time allocating memory: malloc
        dest = malloc(dest_offset + src_size + dest_padding);
    else // Not the first time allocating memory: realloc
        dest = realloc(dest, dest_offset + src_size + dest_padding);
    if(dest == NULL) {
        fprintf(stderr, "malloc/realloc@memapp: Out of memory!\n");
        exit(EXIT_FAILURE);
    }
    memcpy(&(dest[dest_offset]), src, src_size);
    return dest;
}

void set_string(struct mem_block* str, char* data, size_t len) {
    str->ptr = memapp(data, len, str->ptr, 0, 1);
    str->i = len - 1;
}

size_t write_callback_curl(char* buf, size_t size, size_t nmemb, struct mem_block* mem) {
    mem->ptr = memapp(buf, size * nmemb, mem->ptr, mem->i, 0);
    mem->i += size * nmemb;
    return size * nmemb;
}

void print_help() {
    printf("termkdc - A terminal utility for getting and parsing xkcd comics\n\n");
    printf("Program arguments:\n");
    printf("  -h; --help               : Show this help screen\n");
    printf("  -D; --debug              : Show debug info\n");
    printf("  -c <uint>; --comic <uint>: Comic number (0 = lastest comic; default)\n");
    printf("  -d; --date               : Show comic's publish date\n");
    printf("  -t; --title              : Show comic's title\n");
    printf("  -s; --safe-title         : Use safe title instead of regular title (innefective without -t)\n");
    printf("  -T; --transcript         : Show comic's transcript\n");
    printf("  -a; --alt                : Show comic's alt\n");
    printf("  -i; --img                : Show comic's image link\n");
    printf("  -f; --framebuffer        : Render comic strip on framebuffer interactively (fbi-like viewer)\n\n");
    printf("Return values:\n");
    printf("  EXIT_SUCCESS (%i) when no errors occur (warnings don't count as errors)\n", EXIT_SUCCESS);
    printf("  EXIT_FAILURE (%i) when errors occur or when showing this screen involuntarily\n\n", EXIT_FAILURE);
    printf("Guaranteed behaviour on successful exit:\n");
    printf("  The program will always print the comic number first, then anything else.\n");
    printf("  This is useful to keep track of the comics missed in a script. It cannot, however, be disabled.\n\n");
    printf("Example usage:\n");
    printf("  termkcd -c 1000 -t -s -f\n");
    printf("  Prints the safe version of the 1000th comic and views it in framebuffer\n");
}

int get_bit(char bitmap, char n) {
    return (bitmap >> n) & 1;
}

void set_bit(char* bitmap, char n, char b) {
    (*bitmap) ^= (-b ^ (*bitmap)) & (1 << n);
}

int is_arg(const char* arg, const char* a, const char* b) {
    return (strcmp(arg, a) == 0) || (strcmp(arg, b) == 0);
}

unsigned int str_to_uint(const char* str, int* error_flag) {
    size_t len = strlen(str);
    size_t res = 0;
    if(len == 0) {
        (*error_flag) = 1; // 0-length; throw error 1
        return(0);
    }
    for(size_t n = 0; n < len; ++n) {
        if(str[n] >= '0' && str[n] <= '9') {
            if((len - n - 1) == 0)
                res += (unsigned int)(str[n] - '0');
            else {
                unsigned int pow = 10;
                for(size_t i = 1; i < (len - n - 1); ++i)
                    pow *= 10;
                res += (unsigned int)(str[n] - '0') * pow;
            }
        }
        else {
            (*error_flag) = 2; // Invalid char; throw error 2
            return(0);
        }
    }
    if(res > UINT_MAX)
        (*error_flag) = 3; // Too big; throw error 3 (more of a warning than an error, can ignore). Overflowed value still returned.
    return (unsigned int)res;
}

void parse_json(struct mem_block* raw, struct json_parsed* parsed, int debug) {
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

    char buffer[2048]; // A pretty big buffer (2048 bytes)
    size_t buf_n = 0;
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
                set_string(&cur_key, buffer, buf_n);
                buf_n = 0;
            }
            else if(raw->ptr[n] == ',' || raw->ptr[n] == '}') {
                set_bit(&modes, 2, 0);
                buffer[buf_n++] = '\0';
                if(strcmp(cur_key.ptr, "month") == 0)
                    set_string(&parsed->month, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "num") == 0)
                    set_string(&parsed->num, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "link") == 0)
                    set_string(&parsed->link, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "year") == 0)
                    set_string(&parsed->year, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "news") == 0)
                    set_string(&parsed->news, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "safe_title") == 0)
                    set_string(&parsed->safe_title, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "transcript") == 0)
                    set_string(&parsed->transcript, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "alt") == 0)
                    set_string(&parsed->alt, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "img") == 0)
                    set_string(&parsed->img, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "title") == 0)
                    set_string(&parsed->title, buffer, buf_n);
                else if(strcmp(cur_key.ptr, "day") == 0)
                    set_string(&parsed->day, buffer, buf_n);
                else if(debug)
                    fprintf(stderr, "@parse_json: Unknown json key (%s)! Ignoring\n", buffer);
                buf_n = 0;
            }
            else if(raw->ptr[n] >= '0' && raw->ptr[n] <= '9')
                buffer[buf_n++] = raw->ptr[n];
            else if(raw->ptr[n] != ' ' && debug)
                fprintf(stderr, "@parse_json: Unknown char (%c)! Ignoring\n", raw->ptr[n]);
        }
    }
    free(cur_key.ptr);
}

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

png_bytepp load_png(char* png_buf, size_t png_buf_len, png_uint_32* w, png_uint_32* h, int debug) {
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
    (*w) = png_get_image_width(png_ptr, info_ptr);
    (*h) = png_get_image_height(png_ptr, info_ptr);
    png_byte colour_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    // Parse retreived info
    if(bit_depth == 16)
        png_set_strip_16(png_ptr);
    else if(bit_depth < 8)
        png_set_packing(png_ptr);

    if(colour_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if(colour_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    if(colour_type == PNG_COLOR_TYPE_GRAY || colour_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    // Use BGRA mode instead of RGBA, as framebuffers read in BGRA mode and force alpha channel
    if(colour_type == PNG_COLOR_TYPE_RGB || colour_type == PNG_COLOR_TYPE_GRAY || colour_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_set_bgr(png_ptr);

    // Update info
    png_read_update_info(png_ptr, info_ptr);

    // Allocate memory for rows
    png_bytepp row_pointers = png_malloc(png_ptr, (*h) * sizeof(png_bytep));
    for(size_t n = 0; n < *h; ++n)
        row_pointers[n] = malloc(png_get_rowbytes(png_ptr, info_ptr));
    png_set_rows(png_ptr, info_ptr, row_pointers);

    // Read image (pixel data)
    png_read_image(png_ptr, row_pointers);

    // Clean-up (structs, not pixel data!)
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    return row_pointers;
}

int draw_to_fb(png_bytepp image_buffer, size_t w, size_t h) {
    int fd = open("/dev/fb0", O_RDWR); // Open framebuffer device
    if(fd >= 0) { // If the framebuffer device id is >= 0, then it successfully opened
        struct fb_var_screeninfo screen_info;   // Framebuffer screen info
        struct fb_fix_screeninfo fixed_info;    //           ''
        if(!ioctl(fd, FBIOGET_VSCREENINFO, &screen_info) && !ioctl(fd, FBIOGET_FSCREENINFO, &fixed_info)) {
            size_t fb_buflen = screen_info.yres_virtual * fixed_info.line_length;            // Framebuffer buffer length
            char* fb_mem = mmap(NULL, fb_buflen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); // Framebuffer buffer ptr

            if(fb_mem != MAP_FAILED) { // Get access to framebuffer memory
                // Allocate memory for framebuffer restore
                char* fb_mem_old = malloc(fb_buflen);
                if(fb_mem_old == NULL) {
                    // Clean-up
                    munmap(fb_mem, fb_buflen);
                    close(fd);
                    fprintf(stderr, "malloc@draw_to_fb: Out of memory!\n");
                    return 0;
                }

                // Hide cursor and enter newline
                printf("\033[?25l\n");
                
                // Copy buffer
                memcpy(fb_mem_old, fb_mem, fb_buflen);

                // Enter noncanonical input mode
                struct termios termio_old, termio_new;
                tcgetattr(STDIN_FILENO, &termio_old);
                termio_new = termio_old;
                termio_new.c_lflag &= (~ICANON & ECHOE);
                tcsetattr(STDIN_FILENO, TCSANOW, &termio_new);

                // Set up variables
                char* help = "Q: Quit; HJKL: Move comic strip; W: Toggle help";
                size_t help_len = strlen(help);
                int running = 1;
                int show_help = 1;
                const int move_speed = 10;
                size_t xmax = fixed_info.line_length;
                size_t ymax = screen_info.yres_virtual;
                long off_x = (((long)(xmax) / 4) - (long)(w)) / 2;
                if(off_x < 0)
                    off_x = 0;
                long off_y = ((long)(ymax) - (long)(h)) / 2;
                if(off_y < 0)
                    off_y = 0;

                // Main loop
                while(running) {
                    // Draw the image. These calculations are very hacky. They work as expected though... through praying...
                    // TODO: revise this mess
                    size_t size_x = w * 4;
                    if((w + off_x) * 4 > xmax)
                        size_x = xmax - (off_x * 4);
                    size_t this_x = (off_x < 0) ? 0 : off_x;
                    size_t strip_x = 0;
                    if(off_x < 0)
                        strip_x = off_x * -4;
                    if((strip_x < w * 4) && (this_x * 4 <= xmax) && (-off_y < (long)h)) {
                        // Clear framebuffer in areas above and below rendered rows (if any)
                        if(off_y > 0) {
                            size_t y_size = off_y;
                            if(y_size > ymax)
                                y_size = ymax;
                            memset(fb_mem, 0, y_size * xmax);
                        }
                        if((long)(ymax) - (long)(h) - off_y > 0) {
                            long y = (long)(h) + off_y;
                            if(y < 0)
                                y = 0;
                            memset(fb_mem + (y * xmax), 0, (ymax - y) * xmax);
                        }

                        for(size_t y = ((off_y < 0) ? 0 : off_y); (y < (long)(ymax)) && (y < (h + off_y)); ++y) {
                            // Clear the framebuffer line (row)
                            memset(fb_mem + (y * xmax), 0, xmax);
                            // I don't even know... draw image row to framebuffer
                            memcpy(fb_mem + (this_x * 4) + (y * xmax), (image_buffer[y - off_y]) + strip_x, size_x - strip_x);
                        }
                    }
                    else
                        memset(fb_mem, 0, ymax * xmax);

                    // Print help
                    if(show_help)
                        printf("%s", help);

                    // Get keyboard input
                    int wait_for_char = 1;
                    while(wait_for_char) {
                        char c = getchar();
                        switch(c) {
                        case 'q':
                        case 'Q':
                            running = 0;
                            wait_for_char = 0;
                            break;
                        case 'h':
                        case 'H':
                            off_x -= move_speed;
                            if(w > (xmax / 4)) {
                                if((off_x + (long)(w)) < (long)(xmax / 4))
                                    off_x = (long)(xmax / 4) - (long)(w);
                            }
                            else if(off_x < 0)
                                off_x = 0;
                            wait_for_char = 0;
                            break;
                        case 'l':
                        case 'L':
                            off_x += move_speed;
                            if(w > (xmax / 4)) {
                                if(off_x > 0)
                                    off_x = 0;
                            }
                            else if((off_x + (long)(w)) > (long)(xmax / 4))
                                off_x = (long)(xmax / 4) - (long)(w);
                            wait_for_char = 0;
                            break;
                        case 'k':
                        case 'K':
                            off_y -= move_speed;
                            if(h > ymax) {
                                if((off_y + (long)(h)) < (long)(ymax))
                                    off_y = (long)(ymax) - (long)(h);
                            }
                            else if(off_y < 0)
                                off_y = 0;
                            wait_for_char = 0;
                            break;
                        case 'j':
                        case 'J':
                            off_y += move_speed;
                            if(h > ymax) {
                                if(off_y > 0)
                                    off_y = 0;
                            }
                            else if((off_y + (long)(h)) > (long)(ymax))
                                off_y = (long)(ymax) - (long)(h);
                            wait_for_char = 0;
                            break;
                        case 'w':
                        case 'W':
                            if(show_help) {
                                show_help = 0;
                                for(size_t n = 0; n < help_len; ++n)
                                    printf("\b");
                                for(size_t n = 0; n < help_len; ++n)
                                    printf(" ");
                                fflush(stdout);
                            }
                            else {
                                show_help = 1;
                            }
                            wait_for_char = 0;
                            break;
                        }
                    }

                    // Re-position cursor
                    for(size_t n = 0; (n < help_len) && (show_help); ++n)
                        printf("\b");
                }

                // Clear help
                for(size_t n = 0; (n < help_len) && (show_help); ++n)
                    printf(" ");
                for(size_t n = 0; n < help_len; ++n)
                    printf("\b");

                // Show cursor again
                printf("\033[?25h");

                // Exit noncanonical input mode
                tcsetattr(STDIN_FILENO, TCSANOW, &termio_old);
                
                // Restore framebuffer
                memcpy(fb_mem, fb_mem_old, fb_buflen);

                // Unmap framebuffer from memory
                munmap(fb_mem, fb_buflen);

                // Clean-up restore memory
                free(fb_mem_old);
            }
            else {
                close(fd); // Clean-up
                fprintf(stderr, "mmap@draw_to_fb: Could not map framebuffer into memory!\n");
                return 0;
            }
        }
        else {
            fprintf(stderr, "ioctl@draw_to_fb: Could not retreive framebuffer info!\n");
            return 0;
        }
        close(fd); // Close framebuffer so it can be used by other programs. Only needs to be closed if opened successfully.
    }
    else {
        fprintf(stderr, "open@draw_to_fb: Could not open framebuffer device /dev/fb0!\nAre you root or part of the framebuffer's group (typically video)?\n");
        return 0;
    }

    return 1;
}

// Returns EXIT_SUCCESS for success and EXIT_FAILURE for fail
// Will only fail on a parse error, memory error, connection failure or device open failure.
int main(const int argc, const char* argv[]) {
    // Program argument switches
    // 8-bit, 8 bools max. Bitmap order:
    // 0: Debug; -D, --debug
    // 1: Date; -d, --date
    // 2: Safe title; -s, --safe-title
    // 3: Alt; -a, --alt
    // 4: Img; -i, --img
    // 5: Framebuffer; -f, --framebuffer
    // 6: Transcript; -T, --transcript
    // 7: Title; -t, --title
    char switches = 0;
    unsigned long comic = 0;
    int exitcode = EXIT_SUCCESS;

    // Program argument parsing
    for(int n = 1; n < argc; ++n) {
        const char* this_arg = argv[n];
        if(is_arg(this_arg, "-h", "--help")) {
            print_help();
            return EXIT_SUCCESS;
        }
        else if(is_arg(this_arg, "-D", "--debug"))
            set_bit(&switches, 0, 1);
        else if(is_arg(this_arg, "-c", "--comic")) {
            if(++n == argc) {
                printf("Missing value: --comic <uint>\n");
                print_help();
                return EXIT_FAILURE;
            }
            else {
                int errored = 0;
                comic = str_to_uint(argv[n], &errored);
                if(errored == 2) {
                    printf("Invalid value: contains non-numerical characters\n");
                    print_help();
                    return EXIT_FAILURE;
                }
                else if(errored == 3)
                    printf("Warning: Comic number overflowed. Max is %u\n", UINT_MAX);
            }
        }
        else if(is_arg(this_arg, "-d", "--date"))
            set_bit(&switches, 1, 1);
        else if(is_arg(this_arg, "-s", "--safe-title"))
            set_bit(&switches, 2, 1);
        else if(is_arg(this_arg, "-a", "--alt"))
            set_bit(&switches, 3, 1);
        else if(is_arg(this_arg, "-i", "--img"))
            set_bit(&switches, 4, 1);
        else if(is_arg(this_arg, "-f", "--framebuffer"))
            set_bit(&switches, 5, 1);
        else if(is_arg(this_arg, "-T", "--transcript"))
            set_bit(&switches, 6, 1);
        else if(is_arg(this_arg, "-t", "--title"))
            set_bit(&switches, 7, 1);
        else {
            printf("Unknown argument: %s\n", this_arg);
            print_help();
            return EXIT_FAILURE;
        }
    }

    CURL* curl_handle = curl_easy_init();
    if(curl_handle) {
        struct mem_block json_raw = empty_mem;

        long http_status = 0; // HTTP status. Codes which will be checked: 200, 404. Any other status code results in a abort
        if(comic == 0)
            curl_easy_setopt(curl_handle, CURLOPT_URL, "https://xkcd.com/info.0.json"); // Make sure link is https, not http, since it results in a 301
        else {
            char formatted[40]; // 40 chars should be enough, as the max comics would be 4,294,967,295 (max u_int val)
            sprintf(formatted, "https://xkcd.com/%lu/info.0.json", comic);
            curl_easy_setopt(curl_handle, CURLOPT_URL, formatted);
        }
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback_curl);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &json_raw);
        if(get_bit(switches, 0))
            curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);
        CURLcode err = curl_easy_perform(curl_handle);
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_status);
        if(http_status == 200 && err == CURLE_OK) {
            if(json_raw.i > 0 && json_raw.ptr[0] == '{') {
                struct json_parsed json_parsed;
                parse_json(&json_raw, &json_parsed, get_bit(switches, 0));

                // Print comic number (must be done)
                printf("%s\n", json_parsed.num.ptr);

                if(get_bit(switches, 1)) {  // Date
                    if(json_parsed.day.i == 0)
                        set_string(&json_parsed.day, "?", 1);
                    if(json_parsed.month.i == 0)
                        set_string(&json_parsed.month, "?", 1);
                    if(json_parsed.year.i == 0)
                        set_string(&json_parsed.year, "?", 1);
                    printf("%s/%s/%s\n", json_parsed.day.ptr, json_parsed.month.ptr, json_parsed.year.ptr);
                }

                if(get_bit(switches, 7)) {
                    if(get_bit(switches, 2))// Safe-title
                        printf("%s:\n\n", json_parsed.safe_title.ptr);
                    else                    // Title
                        printf("%s:\n\n", json_parsed.title.ptr);
                }

                // Transcript
                if(get_bit(switches, 6))
                    printf("%s\n\n", json_parsed.transcript.ptr);

                if(get_bit(switches, 3))    // Alt text
                    printf("%s\n\n", json_parsed.alt.ptr);

                if(get_bit(switches, 4))    // Comic strip image link
                    printf("%s\n", json_parsed.img.ptr);

                if(get_bit(switches, 5)) {  // Display comic strip to framebuffer
                    // Reset handle props
                    curl_easy_reset(curl_handle);

                    // Set up variables
                    struct mem_block png_buffer = empty_mem;

                    // Configure curl to download comic strip
                    curl_easy_setopt(curl_handle, CURLOPT_URL, json_parsed.img.ptr);
                    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback_curl);
                    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &png_buffer);
                    if(get_bit(switches, 0))
                        curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

                    // Perform curl action
                    err = curl_easy_perform(curl_handle);
                    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_status);

                    // Check if everything went OK
                    if(http_status == 200 && err == CURLE_OK) {
                        // Prepare data for reading PNG retreived
                        // Note: All vars are only initialized on load_png(), now its UB
                        png_uint_32 width = 0;
                        png_uint_32 height = 0;
                        png_bytepp pixel_buffer = NULL;

                        // Load png from memory
                        pixel_buffer = load_png(png_buffer.ptr, png_buffer.i, &width, &height, get_bit(switches, 0));

                        // Draw comic strip from pixel buffer to framebuffer
                        if(pixel_buffer != NULL) {
                            if(!draw_to_fb(pixel_buffer, width, height))
                                exitcode = EXIT_FAILURE;

                            // Free pixel buffer
                            for(size_t n = 0; n < height; ++n)
                                free(pixel_buffer[n]);
                            free(pixel_buffer);
                        }
                    }
                    else {
                        fprintf(stderr, "curl_easy_perform@main: Failed to retrieve comic strip image! HTTP status code: %li\n", http_status);
                        exitcode = EXIT_FAILURE;
                    }

                    // Free png_buffer string
                    free(png_buffer.ptr);
                }

                // Free all strings
                free(json_parsed.month.ptr);
                free(json_parsed.num.ptr);
                free(json_parsed.link.ptr);
                free(json_parsed.year.ptr);
                free(json_parsed.news.ptr);
                free(json_parsed.safe_title.ptr);
                free(json_parsed.transcript.ptr);
                free(json_parsed.alt.ptr);
                free(json_parsed.img.ptr);
                free(json_parsed.title.ptr);
                free(json_parsed.day.ptr);
            }
            else
                fprintf(stderr, "@main: JSON file doesn't start as a table!\n");
        }
        else {
            if(comic != 0 && http_status == 404)
                fprintf(stderr, "Comic %lu doesn't exist!\n", comic);
            else
                fprintf(stderr, "curl_easy_perform@main: Failed to retreive comic %lu! HTTP status code: %li\n", comic, http_status);
            exitcode = EXIT_FAILURE;
        }

        // Free downloaded data buffer
        free(json_raw.ptr);

        // Perform curl cleanup
        curl_easy_cleanup(curl_handle);
    }
    else {
        fprintf(stderr, "curl_easy_init@main: Could not initialize cURL!\n");
        exitcode = EXIT_FAILURE;
    }

    return exitcode;
}
