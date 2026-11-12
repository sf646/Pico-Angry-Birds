/**
 * Slingshot game -- main (Pico 2 / RP2350)
 *
 * Aim + power + fire over serial; one hardcoded level of blocks and pigs.
 *
 * Core 0: serial commands, game rules, drawing (one frame per VGA frame, 60 fps)
 * Core 1: physics (one world step per frame, running while core 0 draws)
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 470 ohm resistor ---> VGA Green
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
 *  - GND     ---> VGA GND
 *  - GPIO 0/1 (UART0 TX/RX) ---> USB-serial adapter, 115200 baud
 */

#include "VGA/vga16_graphics_v3.h"
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/clocks.h"

// Protothreads header defines globals, so include it in exactly ONE .c file.
#include "pt_cornell_rp2040_v1_4.h"

#include "game.h"
#include "input.h"
#include "render.h"

// The buffer the VGA library wants us to draw into this frame (it swaps every frame).
extern char *current_draw_buffer;

// ==================================================
// === core 0 <-> core 1 handshake
// ==================================================
static semaphore_t step_sem;            // core 0 releases it: "run one physics step"
static volatile bool step_done = true;  // core 1 sets it when the step has finished

// Timing, in microseconds (worst case since the last `stats` command)
static volatile uint32_t t_logic_max, t_draw_max, t_phys_max;
static volatile uint32_t frames_total, frames_late;

#define CORE1_STACK_WORDS 2048          // 8 kB: the physics solver needs more than the default 2 kB
static uint32_t core1_stack[CORE1_STACK_WORDS];

static void core1_main(void) {
    while (1) {
        sem_acquire_blocking(&step_sem);
        uint32_t t0 = time_us_32();
        game_physics();
        uint32_t dt = time_us_32() - t0;
        if (dt > t_phys_max) t_phys_max = dt;
        step_done = true;
    }
}

// ==================================================
// === serial command thread
// ==================================================
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt);
    PT_YIELD_usec(1000000);

    static char reply[pt_buffer_size];
    input_handle_line("help", reply, sizeof reply);
    snprintf(pt_serial_out_buffer, pt_buffer_size, "\r\nSlingshot game\r\n%s  stats               frame timing\r\n", reply);
    serial_write;

    while (1) {
        sprintf(pt_serial_out_buffer, "> ");
        serial_write;
        serial_read;
        if (!strcmp(pt_serial_in_buffer, "stats")) {
            // worst-case times since the last time this was asked (a frame is 16667 us)
            snprintf(pt_serial_out_buffer, pt_buffer_size,
                     "worst case, us:  rules %lu   draw %lu   physics %lu   (frame = 16667)\r\n"
                     "frames %lu, of which %lu started late (physics not finished)\r\n",
                     (unsigned long)t_logic_max, (unsigned long)t_draw_max, (unsigned long)t_phys_max,
                     (unsigned long)frames_total, (unsigned long)frames_late);
            t_logic_max = t_draw_max = t_phys_max = 0;
            frames_total = frames_late = 0;
            serial_write;
            continue;
        }
        input_handle_line(pt_serial_in_buffer, reply, sizeof reply);
        if (reply[0]) {
            strcpy(pt_serial_out_buffer, reply);
            serial_write;
        }
    }
    PT_END(pt);
}

// ==================================================
// === game + draw thread (one iteration per VGA frame, 60 fps)
// ==================================================
static PT_THREAD (protothread_game(struct pt *pt))
{
    PT_BEGIN(pt);
    static slingshot_input_t in;

    while (1) {
        // Wait until the buffer we draw into is no longer being displayed
        PT_YIELD_UNTIL(pt, draw_start_signal());
        char *fb = current_draw_buffer;     // the library changes this pointer every frame
        uint32_t t0 = time_us_32();
        frames_total++;

        if (step_done) {
            // Physics is idle: run the game rules, then start the next physics
            // step on core 1. It runs while we draw.
            input_poll(&in);
            game_begin_frame(&in);
            step_done = false;
            sem_release(&step_sem);
        } else {
            // Physics is still busy with the last step (rare). Don't wait for it --
            // just redraw, and the game runs one frame slower.
            frames_late++;
        }
        uint32_t t1 = time_us_32();

        game_draw(fb);

        uint32_t t2 = time_us_32();
        if (t1 - t0 > t_logic_max) t_logic_max = t1 - t0;
        if (t2 - t1 > t_draw_max)  t_draw_max = t2 - t1;
    }
    PT_END(pt);
}

// ========================================
// === main
// ========================================
int main() {
    set_sys_clock_khz(150000, true);
    stdio_init_all();

    initVGA();
    input_init();
    render_init();      // paints the scenery once (a fraction of a second)
    game_init();

    sem_init(&step_sem, 0, 1);
    multicore_reset_core1();
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof core1_stack);

    pt_add_thread(protothread_serial);
    pt_add_thread(protothread_game);
    pt_schedule_start;
}
