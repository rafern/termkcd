#ifndef TERMKCD_FRAMEBUFFER_H
#define TERMKCD_FRAMEBUFFER_H

// Includes for writing to framebuffer
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <unistd.h>
#include <fcntl.h>

// Includes for raw input
#include <termios.h>

// Pre-rendered help text include:
#include "text.h"

int draw_to_fb(unsigned char* image_buffer, size_t w, size_t h) {
    int fd = open("/dev/fb0", O_RDWR); // Open framebuffer device
    if(fd < 0) { // If the framebuffer device id is >= 0, then it successfully opened
        fprintf(stderr, "open@draw_to_fb: Could not open framebuffer device /dev/fb0!\nAre you root or part of the framebuffer's group (typically video)?\n");
        return 0;
    }
    
    struct fb_var_screeninfo var_info; // Framebuffer variable info
    if(ioctl(fd, FBIOGET_VSCREENINFO, &var_info) == -1) {
        fprintf(stderr, "ioctl@draw_to_fb: Could not retreive variable framebuffer info!\n");
        return 0;
    }
    
    // Force 24-bit bit-depth, with RGB colour
    const int bpp = 4; // BYTES per pixel, not BITS per pixel
    var_info.bits_per_pixel = bpp * 8;
    var_info.grayscale = 0;
    if(ioctl(fd, FBIOPUT_VSCREENINFO, &var_info) == -1) {
        close(fd); // Clean-up
        fprintf(stderr, "ioctl@draw_to_fb: Could not set variable framebuffer info!\n");
        return 0;
    }

    struct fb_fix_screeninfo fix_info; // Framebuffer fixed info
    if(ioctl(fd, FBIOGET_FSCREENINFO, &fix_info) == -1) {
        close(fd); // Clean-up
        fprintf(stderr, "ioctl@draw_to_fb: Could not retreive fixed framebuffer info!\n");
        return 0;
    }

    size_t fb_buflen = var_info.yres_virtual * fix_info.line_length; // Framebuffer buffer length (of each sub-buffer)
    unsigned char* fb_mem = mmap(0, fb_buflen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); // Framebuffer buffer ptr

    if(fb_mem == MAP_FAILED) { // Get access to framebuffer memory
        close(fd); // Clean-up
        fprintf(stderr, "mmap@draw_to_fb: Could not map framebuffer into memory!\n");
        return 0;
    }

    // Allocate memory for framebuffer restore
    unsigned char* fb_mem_old = malloc(fb_buflen);
    if(fb_mem_old == NULL) {
        // Clean-up
        munmap(fb_mem, fb_buflen);
        close(fd);
        fprintf(stderr, "malloc@draw_to_fb: Out of memory!\n");
        return 0;
    }

    unsigned char* backbuffer = malloc(fb_buflen);
    if(fb_mem_old == NULL) {
        // Clean-up
        munmap(fb_mem, fb_buflen);
        close(fd);
        free(fb_mem_old);
        fprintf(stderr, "malloc@draw_to_fb: Out of memory!\n");
        return 0;
    }

    // Clear current buffer, with it being fully black
    memset(backbuffer, 0, fb_buflen);

    // Hide cursor
    printf("\033[?25l");
    
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
    char running = 1;
    char show_help = 1;

    // Constant variables for convenience (these should be optimised out by the compiler)
    const int move_speed = 10;
    const int toolbar_text_off_x = 2;
    const int toolbar_text_off_y = 2;
    const int toolbar_text_thickness = 2;
    const int toolbar_size = 28;
    const int toolbar_border_thickness = 2;
    const float toolbar_falpha = .75f;
    const float toolbar_falpha_spare = 1.f - toolbar_falpha;
    const unsigned char toolbar_alpha = 255 * toolbar_falpha;
    const float toolbar_fcolour = .15f;
    const unsigned char toolbar_colour = 255 * toolbar_fcolour;
    const unsigned char toolbar_colour_backed = toolbar_falpha * toolbar_fcolour * 255;
    const unsigned char toolbar_border_colour = toolbar_colour_backed * 0.75;
    const size_t ll = fix_info.line_length;
    const int xmax = var_info.xres;
    const int ymax = var_info.yres;
    
    // Set-up offset variables
    int off_x = (xmax - (int)w) / 2;
    if(off_x < 0)
        off_x = 0;
    int off_y = (ymax - (int)h) / 2;
    if(off_y < toolbar_size)
        off_y = toolbar_size;

    // Note: next vars are only defined in the main loop
    // Framebuffer positions:
    int fb_l; // Left
    int fb_r; // Right
    int fb_t; // Top
    int fb_b; // Bottom
    // Bitmap subimage positions:
    int bmp_x; // X subimage start
    int bmp_y; // Y subimage start
    int bmp_w; // Subimage width
    int bmp_h; // Subimage height

    // Main loop
    while(running) {
        // Calculate positions (framebuffer)
        fb_l = off_x;
        if(fb_l < 0)
            fb_l = 0;

        fb_r = off_x + (int)w;
        if(fb_r > xmax)
            fb_r = xmax;

        fb_t = off_y;
        if(fb_t < 0)
            fb_t = 0;

        fb_b = off_y + (int)h;
        if(fb_b > ymax)
            fb_b = ymax;

        // Calculate more positions (subimage)
        bmp_x = fb_l - off_x;
        bmp_y = fb_t - off_y;
        bmp_w = fb_r - fb_l;
        bmp_h = fb_b - fb_t;

        // Copy subimage to current buffer
        if((bmp_w > 0) && (bmp_h > 0)) {
            for(size_t y = 0; y < bmp_h; ++y) // Copy the subimage row to the current buffer
                stride_memcpy(backbuffer + (fb_l * bpp) + ((y + fb_t) * ll), image_buffer + ((y + bmp_y) * w * 3) + (bmp_x * 3), bmp_w, bpp, 3);
        }

        // Print .-@~:fancy:~@-. version of the help toolbar
        if(show_help) {
            // Do the transparent toolbar box
            for(size_t y = 0; y < toolbar_size; ++y) {
                if(y + 3 >= toolbar_size)
                    stride_memset(backbuffer + (y * ll), toolbar_border_colour, 3, xmax, bpp);
                else { // This should be very expensive as it is alpha blending on CPU, so beware
                    if((y < fb_t) || (y > fb_b)) // Cheaper, non-blending version
                        stride_memset(backbuffer + (y * ll), toolbar_colour_backed, 3, xmax, bpp);
                    else {
                        // Part before image intersection
                        if(fb_l > 0)
                            stride_memset(backbuffer + (y * ll), toolbar_colour_backed, 3, fb_l, bpp);
                        // Image intersection (Blend here)
                        // Alpha blending: RGB=alpha * srcRGB + destRGB * (1 - alpha) == RGB=backed_srcRGB + destRGB * alpha_spare
                        for(size_t x = fb_l; x <= fb_r; ++x) {
                            const size_t off = (y * ll) + (x * bpp);
                            backbuffer[off] = toolbar_colour_backed + backbuffer[off] * toolbar_falpha_spare;
                            backbuffer[off + 1] = toolbar_colour_backed + backbuffer[off + 1] * toolbar_falpha_spare;
                            backbuffer[off + 2] = toolbar_colour_backed + backbuffer[off + 2] * toolbar_falpha_spare;
                        }
                        // Part after image intersection
                        if(fb_r < xmax)
                            stride_memset(backbuffer + (y * ll) + (fb_r * bpp), toolbar_colour_backed, 3, xmax - fb_r, bpp);
                    }
                }
            }
    
            // Print the pre-rendered text
            for(size_t x = 0; x < termkcd_fb_help_text_width; ++x) {
                for(size_t y = 0; y < termkcd_fb_help_text_height; ++y) {
                    if(termkcd_fb_help_text[y * termkcd_fb_help_text_width + x] != 0) {
                        for(int n = 0; n < toolbar_text_thickness; ++n)
                            stride_memset(backbuffer + ((y * toolbar_text_thickness + toolbar_text_off_y + n) * ll) + ((x * toolbar_text_thickness + toolbar_text_off_x) * bpp), 255, 3, 2, bpp);
                    }
                }
            }
        }
        // End of .-@~:fancyness:~@-. (im bad at this fancy nonsense, ok?)

        // "Swap" buffers
        memcpy(fb_mem, backbuffer, fb_buflen);

        // Clear screen (only in area at which the comic strip was drawn)
        if((bmp_w > 0) && (bmp_h > 0)) {
            for(size_t y = 0; y < bmp_h; ++y)
                stride_memset(backbuffer + (fb_l * bpp) + ((y + fb_t) * ll), 0, 3, bmp_w, bpp);
        }

        // Get keyboard input
        char wait_for_char = 1;
        while(wait_for_char) {
            int old_off_x = off_x;
            int old_off_y = off_y;
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
                if(w > xmax) {
                    if((off_x + (int)(w)) < xmax)
                        off_x = xmax - (int)(w);
                }
                else if(off_x < 0)
                    off_x = 0;
                wait_for_char = 0;
                break;
            case 'l':
            case 'L':
                off_x += move_speed;
                if(w > xmax) {
                    if(off_x > 0)
                        off_x = 0;
                }
                else if((off_x + (int)(w)) > xmax)
                    off_x = xmax - (int)(w);
                break;
            case 'k':
            case 'K':
                off_y -= move_speed;
                if(h > ymax) {
                    if((off_y + (int)(h)) < ymax)
                        off_y = ymax - (int)(h);
                }
                else if(off_y < toolbar_size)
                    off_y = toolbar_size;
                break;
            case 'j':
            case 'J':
                off_y += move_speed;
                if(h > ymax) {
                    if(off_y > toolbar_size)
                        off_y = toolbar_size;
                }
                else if((off_y + (int)(h)) > ymax)
                    off_y = ymax - (int)(h);
                break;
            case 'w':
            case 'W':
                if(show_help) {
                    show_help = 0;
                    // Clear the toolbar's previous area
                    stride_memset(backbuffer, 0, 3, xmax * toolbar_size, bpp);
                }
                else
                    show_help = 1;
                wait_for_char = 0;
                break;
            }

            if(old_off_x != off_x || old_off_y != off_y)
                wait_for_char = 0;
        }
    }

    // Show cursor again
    printf("\033[?25h");

    // Exit noncanonical input mode
    tcsetattr(STDIN_FILENO, TCSANOW, &termio_old);
    
    // Restore framebuffer
    memcpy(fb_mem, fb_mem_old, fb_buflen);

    // Unmap framebuffer from memory
    munmap(fb_mem, fb_buflen);

    // Close framebuffer device
    close(fd);

    // Clean-up restore memory and backbuffer
    free(fb_mem_old);
    free(backbuffer);

    return 1;
}

#endif
