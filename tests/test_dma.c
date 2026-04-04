/* test_dma.c — standalone DMA HUB75 test, no networking required.
 *
 * Lights panel 0 with a red/green/blue column pattern and panel 1 with
 * a white top-half / black bottom-half pattern, holds for 10 seconds.
 *
 * Run: sudo ./build/test_dma
 */
#include "../include/hub75_dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PANEL_W  128
#define PANEL_H   64   /* full panel height (both halves) */

static hub75_rgb_t panel0[PANEL_H * PANEL_W];
static hub75_rgb_t panel1[PANEL_H * PANEL_W];

int main(void) {
    /* --- Build panel 0: RGB column stripes --- */
    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_W; x++) {
            hub75_rgb_t px = {0, 0, 0};
            int third = PANEL_W / 3;
            if      (x < third)           px = (hub75_rgb_t){255,   0,   0}; /* red   */
            else if (x < 2 * third)       px = (hub75_rgb_t){  0, 255,   0}; /* green */
            else                          px = (hub75_rgb_t){  0,   0, 255}; /* blue  */
            panel0[y * PANEL_W + x] = px;
        }
    }

    /* --- Build panel 1: white top half, black bottom half --- */
    for (int y = 0; y < PANEL_H; y++) {
        hub75_rgb_t px = (y < PANEL_H / 2)
            ? (hub75_rgb_t){255, 255, 255}
            : (hub75_rgb_t){  0,   0,   0};
        for (int x = 0; x < PANEL_W; x++)
            panel1[y * PANEL_W + x] = px;
    }

    /* --- Init DMA driver --- */
    hub75_dma_state_t dma;
    if (hub75_dma_init(&dma, 5) != 0) {
        fprintf(stderr, "hub75_dma_init failed\n");
        return 1;
    }

    /* --- Push pixel data --- */
    hub75_dma_update_panels(&dma, panel0, panel1);
    printf("Pattern loaded. Displaying for 10 seconds...\n");
    fflush(stdout);

    sleep(10);

    hub75_dma_shutdown(&dma);
    printf("Done.\n");
    return 0;
}
