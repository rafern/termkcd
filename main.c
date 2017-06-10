/*
 * 2017, Rafael Fernandes, public domain software:
 *        ///// USE AT YOUR OWN RISK! /////
 */

// General includes
#include <stdlib.h>
#include <stdio.h>

// termkcd includes
#include "memory.h"
#include "util.h"
#include "web.h"
#include "image.h"
#include "framebuffer.h"

// Commit changes:
//  #1:
//   - Prevented possible buffer overflow by implementing a dynamic parse_json buffer size (not fixed to 2048 bytes anymore)
//   - Everything now returns instead exiting on critical errors to properly free memory
//  ~~~~~
//  #2:
//   Image support:
//   - Added a file extension check to know which image library to use
//   - Added support for jpeg
//   
//   Memory management:
//   - After images are loaded, they are now converted into BGR bitmaps, instead of BGRA png rows, to save memory and to be JPEG compatible (framebuffer copying should now be slower due to this)
//   - Memory operations now use void* as arguments
//   - Images are now loaded row by row to (kinda) save memory
//
//   Image viewer:
//   - Bit-depth and colour mode now forced to 24bit RGB
//   - Revised framebuffer calculations, making it cleaner
//   - Framebuffer rendering is now double-buffered, more memory required as a consequence (maybe make it toggleable in the future?)
//   - Framebuffer viewer no longer needs to depend on terminal text to display help as it uses a pre-rendered help text in a bitmap.
//
//   Misc:
//   - Re-organised some code
//   - Separated code into different header files for better read-ability (possible todo: make source files for each header)

// TODO (* have high priority):
// * Make comic number printing optional
// * Make program arguments which are single-characters chain-able
// - Add sources for each header
// - Turn everything into smaller functions
// - Make code more DRY, by generalising functions (especially memory.h functions)

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

int is_arg(const char* arg, const char* a, const char* b) {
    return (strcmp(arg, a) == 0) || (strcmp(arg, b) == 0);
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
                if(parse_json(&json_raw, &json_parsed, get_bit(switches, 0))) {
                    // Print comic number (must be done)
                    printf("%s\n", json_parsed.num.ptr);

                    if(get_bit(switches, 1)) {  // Date
                        printf("%s/%s/%s\n", (json_parsed.day.i == 0) ? "?" : json_parsed.day.ptr
                                           , (json_parsed.month.i == 0) ? "?" : json_parsed.month.ptr
                                           , (json_parsed.year.i == 0) ? "?" : json_parsed.year.ptr);
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
                        enum file_ext extension = get_extension(&json_parsed.img); // Check file extension
                        if(extension == FILE_EXT_UNKNOWN) { // Unknown file extension
                            fprintf(stderr, "get_extension@main: The image has an unsupported extension!\n");
                            exitcode = EXIT_FAILURE;
                        }
                        else { // Valid file extension
                            // Reset handle props
                            curl_easy_reset(curl_handle);

                            // Set up variables
                            struct mem_block file_buffer = empty_mem;

                            // Configure curl to download comic strip
                            curl_easy_setopt(curl_handle, CURLOPT_URL, json_parsed.img.ptr);
                            curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback_curl);
                            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &file_buffer);
                            if(get_bit(switches, 0))
                                curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

                            // Perform curl action
                            err = curl_easy_perform(curl_handle);
                            curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_status);

                            // Check if everything went OK
                            if(http_status == 200 && err == CURLE_OK) {
                                // Prepare data for reading file retreived
                                long unsigned int width = 0;
                                long unsigned int height = 0;
                                // RGB bitmap
                                unsigned char* bitmap_buffer = NULL;

                                if(extension == FILE_EXT_PNG) { // Load using libpng, as it has a PNG file extension
                                    // Load png from memory
                                    bitmap_buffer = load_png(file_buffer.ptr, file_buffer.i, &width, &height);
                                }
                                // Note: this else statement may be a problem in the future when more file types are used
                                else { // Load using libjpeg, as it has a JPEG file extension.
                                    bitmap_buffer = load_jpeg(file_buffer.ptr, file_buffer.i, &width, &height);
                                }

                                // Draw comic strip from bitmap buffer to framebuffer
                                if(bitmap_buffer != NULL) {
                                    if(!draw_to_fb(bitmap_buffer, width, height))
                                        exitcode = EXIT_FAILURE;
                                    free(bitmap_buffer);
                                }
                            }
                            else {
                                if(err == CURLE_WRITE_ERROR)
                                    fprintf(stderr, "curl_easy_perform@main: Failed to copy received data to memory!\n");
                                else
                                    fprintf(stderr, "curl_easy_perform@main: Failed to retrieve comic strip image! HTTP status code: %li\n", http_status);
                                exitcode = EXIT_FAILURE;
                            }

                            // Free file_buffer mem_block
                            free(file_buffer.ptr);
                        }
                    }
                }
                else {
                    fprintf(stderr, "parse_json@main: Failed to parse JSON!");
                    exitcode = EXIT_FAILURE;
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
            if(err == CURLE_WRITE_ERROR)
                fprintf(stderr, "curl_easy_perform@main: Failed to copy received data to memory!\n");
            else if(comic != 0 && http_status == 404)
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
