#include "../include/hub75_dma.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PANEL_W 128
#define PANEL_H  64

int main(void) {
    hub75_dma_state_t dma;
    if (hub75_dma_init(&dma, 5) != 0) return 1;

    hub75_rgb_t panel[PANEL_H * PANEL_W];
    hub75_rgb_t blank[PANEL_H * PANEL_W];
    memset(blank, 0, sizeof(blank));

    const char *names[] = {"RED", "GREEN", "BLUE"};
    hub75_rgb_t colors[] = {{255,0,0}, {0,255,0}, {0,0,255}};

    for (int c = 0; c < 3; c++) {
        printf("Showing %s for 3 seconds...\n", names[c]);
        fflush(stdout);
        for (int i = 0; i < PANEL_H * PANEL_W; i++) panel[i] = colors[c];
        hub75_dma_update_panels(&dma, panel, blank);
        sleep(3);
    }

    printf("Blanking.\n");
    hub75_dma_update_panels(&dma, blank, blank);
    sleep(1);
    hub75_dma_shutdown(&dma);
    return 0;
}
