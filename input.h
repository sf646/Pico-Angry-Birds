// input.h -- where the game gets aim / power / fire from.
//
// The game only ever calls input_poll(). Right now the values come from serial
// commands (input_handle_line). Later the same struct will be filled from the
// slingshot Pico (UART / WiFi) without changing any game code.
#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float angle_deg;  // aim angle above horizontal, ANGLE_MIN_DEG..ANGLE_MAX_DEG
    float power;      // pull strength, 0.0 .. 1.0
    bool  fire;       // true exactly once per fire request
    bool  reset;      // true exactly once per reset request (rebuild level)
} slingshot_input_t;

void input_init(void);

// Copy the current input. The fire/reset flags are consumed (cleared) by this call.
void input_poll(slingshot_input_t *out);

// --- serial backend ---
// Parse one typed line (e.g. "angle 45", "power 80", "fire") and write a
// human-readable reply into `reply`.
void input_handle_line(const char *line, char *reply, size_t reply_len);

#endif
