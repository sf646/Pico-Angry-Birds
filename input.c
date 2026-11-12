// input.c -- serial-command implementation of the slingshot input
#include "input.h"
#include "game.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static float s_angle_deg;
static float s_power;
static bool  s_fire;
static bool  s_reset;

void input_init(void) {
    s_angle_deg = 45.0f;
    s_power     = 0.5f;
    s_fire      = false;
    s_reset     = false;
}

void input_poll(slingshot_input_t *out) {
    out->angle_deg = s_angle_deg;
    out->power     = s_power;
    out->fire      = s_fire;
    out->reset     = s_reset;
    s_fire  = false;  // one-shot
    s_reset = false;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void input_handle_line(const char *line, char *reply, size_t reply_len) {
    char cmd[16] = {0};
    float value = 0.0f;
    int n = sscanf(line, "%15s %f", cmd, &value);

    if (n < 1) {  // empty line
        reply[0] = '\0';
        return;
    }
    for (char *c = cmd; *c; c++) *c = (char)tolower((unsigned char)*c);

    if (!strcmp(cmd, "angle") || !strcmp(cmd, "a")) {
        if (n < 2) {
            snprintf(reply, reply_len, "usage: angle <%.0f-%.0f>\r\n", ANGLE_MIN_DEG, ANGLE_MAX_DEG);
            return;
        }
        s_angle_deg = clampf(value, ANGLE_MIN_DEG, ANGLE_MAX_DEG);
        snprintf(reply, reply_len, "angle = %.0f deg\r\n", s_angle_deg);
    }
    else if (!strcmp(cmd, "power") || !strcmp(cmd, "p")) {
        if (n < 2) {
            snprintf(reply, reply_len, "usage: power <0-100>\r\n");
            return;
        }
        s_power = clampf(value, 0.0f, 100.0f) / 100.0f;
        snprintf(reply, reply_len, "power = %.0f%%\r\n", s_power * 100.0f);
    }
    else if (!strcmp(cmd, "fire") || !strcmp(cmd, "f")) {
        s_fire = true;
        snprintf(reply, reply_len, "fire!\r\n");
    }
    else if (!strcmp(cmd, "reset") || !strcmp(cmd, "r")) {
        s_reset = true;
        snprintf(reply, reply_len, "level reset\r\n");
    }
    else if (!strcmp(cmd, "help") || !strcmp(cmd, "h") || !strcmp(cmd, "?")) {
        snprintf(reply, reply_len,
                 "commands:\r\n"
                 "  angle <0-90>   (a)  aim angle in degrees\r\n"
                 "  power <0-100>  (p)  pull strength in percent\r\n"
                 "  fire           (f)  launch the bird\r\n"
                 "  reset          (r)  rebuild the level\r\n"
                 "  help           (h)  this list\r\n");
    }
    else {
        snprintf(reply, reply_len, "unknown command '%s' -- type help\r\n", cmd);
    }
}
