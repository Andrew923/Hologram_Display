/* test_dma_rate.c — measure hub75_dma_update_panels throughput.
 *
 * Alternates between two pre-built pixel buffers (solid red / solid blue)
 * and measures how many updates/sec are achievable.
 *
 * Run: sudo ./build/test_dma_rate
 */
#include "../include/hub75_dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PANEL_W  128
#define PANEL_H   64

static hub75_rgb_t buf_red [PANEL_H * PANEL_W];
static hub75_rgb_t buf_blue[PANEL_H * PANEL_W];

static int64_t mono_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int main(void) {
    /* Pre-build two solid-color slices */
    for (int i = 0; i < PANEL_H * PANEL_W; i++) {
        buf_red [i] = (hub75_rgb_t){255,   0,   0};
        buf_blue[i] = (hub75_rgb_t){  0,   0, 255};
    }

    hub75_dma_state_t dma;
    if (hub75_dma_init(&dma, 5) != 0) {
        fprintf(stderr, "hub75_dma_init failed\n");
        return 1;
    }

    printf("Running rate test for 5 seconds...\n");
    fflush(stdout);

    const int64_t TEST_US = 5000000;
    int64_t start = mono_us();
    int64_t last_print = start;
    long frames = 0;
    long total_frames = 0;

    while (1) {
        int64_t now = mono_us();
        int64_t elapsed = now - start;
        if (elapsed >= TEST_US) break;

        hub75_rgb_t *slice = (frames % 2 == 0) ? buf_red : buf_blue;
        hub75_dma_update_panels(&dma, slice, slice);
        frames++;
        total_frames++;

        /* Print every second */
        if (now - last_print >= 1000000) {
            printf("  %ld updates/sec  (%.2f ms/update)\n",
                   frames, 1000.0 / frames);
            fflush(stdout);
            frames = 0;
            last_print = now;
        }
    }

    int64_t total_us = mono_us() - start;
    printf("Total: %ld updates in %.3f s = %.0f updates/sec  (%.3f ms/update)\n",
           total_frames,
           total_us / 1e6,
           total_frames / (total_us / 1e6),
           total_us / 1e3 / total_frames);

    hub75_dma_shutdown(&dma);
    return 0;
}
