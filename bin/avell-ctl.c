/*
 * avell-ctl: Hardware control tool for Avell A60 MUV (ITE 8291 Rev 0.03)
 * Controls keyboard backlight RGB, effects, brightness, and system power profiles.
 *
 * MIT License
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID   0x048D
#define PRODUCT_ID  0xCE00

#define ITE_INTERFACE       1
#define ITE_EP_OUT          0x02
#define ITE_EP_IN           0x81

#define CMD_SET_EFFECT      8
#define CMD_SET_BRIGHTNESS  9
#define CMD_SET_PALETTE     20
#define CMD_SET_ROW_INDEX   22
#define CMD_GET_FW_VERSION  128
#define CMD_GET_EFFECT      136

#define NUM_ROWS 6
#define NUM_COLS 21
#define ROW_BUFFER_LEN (3 * NUM_COLS + 2) // 65 bytes
#define ROW_BLUE_OFFSET   (1 + 0 * NUM_COLS) // 1
#define ROW_GREEN_OFFSET  (1 + 1 * NUM_COLS) // 22
#define ROW_RED_OFFSET    (1 + 2 * NUM_COLS) // 43

typedef struct {
    uint8_t r, g, b;
} RGBColor;

static const struct {
    const char *name;
    uint8_t code;
    uint8_t r, g, b;
} COLOR_TABLE[] = {
    { "none",    0,   0,   0,   0 },
    { "red",     1, 255,   0,   0 },
    { "orange",  2, 255, 100,   0 },
    { "yellow",  3, 255, 200,   0 },
    { "green",   4,   0, 255,   0 },
    { "blue",    5,   0,   0, 255 },
    { "teal",    6,   0, 255, 255 },
    { "purple",  7, 255,   0, 255 },
    { "random",  8, 255, 255, 255 },
    { "white",   0, 255, 255, 255 },
    { "pink",    0, 255,  50, 150 },
    { "gold",    0, 255, 215,   0 },
    { "cyan",    6,   0, 255, 255 },
    { NULL,      0,   0,   0,   0 }
};

static const struct {
    const char *name;
    uint8_t id;
} EFFECT_TABLE[] = {
    { "breathing",      0x02 },
    { "wave",           0x03 },
    { "random",         0x04 },
    { "rainbow",        0x05 },
    { "ripple",         0x06 },
    { "reactiveripple", 0x07 },
    { "marquee",        0x09 },
    { "raindrop",       0x0A },
    { "aurora",         0x0E },
    { "fireworks",      0x11 },
    { "user",           0x33 },
    { NULL,             0 }
};

static int parse_color_name_or_hex(const char *str, RGBColor *color, uint8_t *code_out) {
    if (!str || !color) return -1;

    // Check hex #RRGGBB or RRGGBB
    if (str[0] == '#') str++;
    if (strlen(str) == 6) {
        unsigned int r, g, b;
        if (sscanf(str, "%02x%02x%02x", &r, &g, &b) == 3) {
            color->r = (uint8_t)r;
            color->g = (uint8_t)g;
            color->b = (uint8_t)b;
            if (code_out) *code_out = 8; // custom/random
            return 0;
        }
    }

    // Check r,g,b
    unsigned int r, g, b;
    if (sscanf(str, "%u,%u,%u", &r, &g, &b) == 3) {
        color->r = (uint8_t)(r > 255 ? 255 : r);
        color->g = (uint8_t)(g > 255 ? 255 : g);
        color->b = (uint8_t)(b > 255 ? 255 : b);
        if (code_out) *code_out = 8;
        return 0;
    }

    // Check table
    for (int i = 0; COLOR_TABLE[i].name != NULL; i++) {
        if (strcasecmp(COLOR_TABLE[i].name, str) == 0) {
            color->r = COLOR_TABLE[i].r;
            color->g = COLOR_TABLE[i].g;
            color->b = COLOR_TABLE[i].b;
            if (code_out) *code_out = COLOR_TABLE[i].code;
            return 0;
        }
    }

    return -1;
}

static uint8_t parse_effect_name(const char *name) {
    if (!name) return 0;
    for (int i = 0; EFFECT_TABLE[i].name != NULL; i++) {
        if (strcasecmp(EFFECT_TABLE[i].name, name) == 0) {
            return EFFECT_TABLE[i].id;
        }
    }
    return 0;
}

static const char* get_effect_name(uint8_t id) {
    for (int i = 0; EFFECT_TABLE[i].name != NULL; i++) {
        if (EFFECT_TABLE[i].id == id) {
            return EFFECT_TABLE[i].name;
        }
    }
    return "unknown";
}

// -------------------------------------------------------------
// USB Communication
// -------------------------------------------------------------

typedef struct {
    libusb_context *ctx;
    libusb_device_handle *handle;
    bool claimed;
} ITEController;

static int ite_open(ITEController *dev) {
    memset(dev, 0, sizeof(*dev));

    int r = libusb_init(&dev->ctx);
    if (r < 0) return r;

    libusb_device **list = NULL;
    ssize_t cnt = libusb_get_device_list(dev->ctx, &list);
    bool found = false;
    libusb_device *target_device = NULL;

    if (cnt > 0) {
        for (ssize_t i = 0; i < cnt; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(list[i], &desc) == 0) {
                if (desc.idVendor == VENDOR_ID && desc.idProduct == PRODUCT_ID) {
                    found = true;
                    target_device = list[i];
                    break;
                }
            }
        }
    }

    if (!found) {
        if (list) libusb_free_device_list(list, 1);
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
        return LIBUSB_ERROR_NO_DEVICE;
    }

    r = libusb_open(target_device, &dev->handle);
    if (list) libusb_free_device_list(list, 1);

    if (r != 0 || !dev->handle) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
        return (r != 0) ? r : LIBUSB_ERROR_ACCESS;
    }

    libusb_set_auto_detach_kernel_driver(dev->handle, 1);
    r = libusb_claim_interface(dev->handle, ITE_INTERFACE);
    if (r < 0) {
        libusb_close(dev->handle);
        libusb_exit(dev->ctx);
        dev->handle = NULL;
        dev->ctx = NULL;
        return r;
    }

    dev->claimed = true;
    return 0;
}

static void ite_close(ITEController *dev) {
    if (dev->handle) {
        if (dev->claimed) {
            libusb_release_interface(dev->handle, ITE_INTERFACE);
            dev->claimed = false;
        }
        libusb_close(dev->handle);
        dev->handle = NULL;
    }
    if (dev->ctx) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
    }
}

static int ite_send_ctrl(ITEController *dev, const uint8_t *payload, int len) {
    uint8_t buf[8] = {0};
    if (len > 8) len = 8;
    memcpy(buf, payload, len);

    return libusb_control_transfer(
        dev->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
        0x09, // HID Set_Report
        0x0300, // Feature report
        0x0001, // Interface 1
        buf,
        8,
        1000
    );
}

static int ite_get_ctrl(ITEController *dev, uint8_t *buf, int len) {
    return libusb_control_transfer(
        dev->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_IN,
        0x01, // HID Get_Report
        0x0300, // Feature report
        0x0001, // Interface 1
        buf,
        len,
        1000
    );
}

static int ite_send_bulk(ITEController *dev, const uint8_t *data, int len) {
    int transferred = 0;
    return libusb_bulk_transfer(dev->handle, ITE_EP_OUT, (unsigned char*)data, len, &transferred, 1000);
}

// Set full keyboard effect
static int ite_set_effect_raw(ITEController *dev, uint8_t control, uint8_t effect, uint8_t speed,
                             uint8_t brightness, uint8_t color, uint8_t dir_reactive, uint8_t save) {
    uint8_t cmd[8] = {
        CMD_SET_EFFECT,
        control,
        effect,
        speed,
        brightness,
        color,
        dir_reactive,
        save
    };
    return ite_send_ctrl(dev, cmd, sizeof(cmd));
}

static int ite_turn_off(ITEController *dev) {
    return ite_set_effect_raw(dev, 0x01, 0, 0, 0, 0, 0, 0);
}

static int ite_turn_on(ITEController *dev) {
    // Restore default wave or rainbow if no effect
    return ite_set_effect_raw(dev, 0x02, 0x05, 5, 25, 8, 0, 0);
}

static int ite_set_brightness(ITEController *dev, uint8_t brightness) {
    if (brightness > 50) brightness = 50;
    uint8_t cmd[8] = { CMD_SET_BRIGHTNESS, 0x02, brightness, 0, 0, 0, 0, 0 };
    return ite_send_ctrl(dev, cmd, sizeof(cmd));
}

static int ite_set_static_color(ITEController *dev, RGBColor color, uint8_t brightness, bool save) {
    if (brightness > 50) brightness = 50;

    // Enable user color mode (effect 0x33)
    int r = ite_set_effect_raw(dev, 0x02, 0x33, 0, brightness, 0, 0, save ? 1 : 0);
    if (r < 0) return r;

    // Fill each row 0..5
    for (int row = 0; row < NUM_ROWS; row++) {
        uint8_t row_cmd[8] = { CMD_SET_ROW_INDEX, 0x00, (uint8_t)row, 0, 0, 0, 0, 0 };
        r = ite_send_ctrl(dev, row_cmd, sizeof(row_cmd));
        if (r < 0) return r;

        uint8_t buf[ROW_BUFFER_LEN] = {0};
        for (int col = 0; col < NUM_COLS; col++) {
            buf[ROW_BLUE_OFFSET + col]  = color.b;
            buf[ROW_GREEN_OFFSET + col] = color.g;
            buf[ROW_RED_OFFSET + col]   = color.r;
        }

        r = ite_send_bulk(dev, buf, sizeof(buf));
        if (r < 0) return r;
    }

    return 0;
}

static int ite_query_status(ITEController *dev, bool *is_on, uint8_t *effect_id,
                           uint8_t *speed, uint8_t *brightness, uint8_t *color_id) {
    uint8_t cmd[8] = { CMD_GET_EFFECT, 0, 0, 0, 0, 0, 0, 0 };
    int r = ite_send_ctrl(dev, cmd, sizeof(cmd));
    if (r < 0) return r;

    uint8_t resp[8] = {0};
    r = ite_get_ctrl(dev, resp, sizeof(resp));
    if (r < 0) return r;

    if (is_on) *is_on = (resp[1] == 0x02);
    if (effect_id) *effect_id = resp[2];
    if (speed) *speed = resp[3];
    if (brightness) *brightness = resp[4];
    if (color_id) *color_id = resp[5];

    return 0;
}

// -------------------------------------------------------------
// System Telemetry & Power Profiles
// -------------------------------------------------------------

static int get_cpu_temp_celsius(void) {
    // Check hwmon coretemp
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return -1;

    struct dirent *ent;
    int temp = -1;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "hwmon", 5) != 0) continue;

        char name_path[512];
        snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%s/name", ent->d_name);
        FILE *fn = fopen(name_path, "r");
        if (fn) {
            char name[64] = {0};
            if (fgets(name, sizeof(name), fn)) {
                if (strncmp(name, "coretemp", 8) == 0 || strncmp(name, "acpitz", 6) == 0) {
                    char temp_path[512];
                    snprintf(temp_path, sizeof(temp_path), "/sys/class/hwmon/%s/temp1_input", ent->d_name);
                    FILE *ft = fopen(temp_path, "r");
                    if (ft) {
                        int mdeg = 0;
                        if (fscanf(ft, "%d", &mdeg) == 1) {
                            temp = mdeg / 1000;
                            fclose(ft);
                            fclose(fn);
                            break;
                        }
                        fclose(ft);
                    }
                }
            }
            fclose(fn);
        }
    }
    closedir(dir);
    return temp;
}

static void get_battery_info(int *cap, char *status, size_t status_len) {
    if (cap) *cap = -1;
    if (status && status_len > 0) snprintf(status, status_len, "Unknown");

    FILE *fc = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (fc) {
        int c = 0;
        if (fscanf(fc, "%d", &c) == 1 && cap) *cap = c;
        fclose(fc);
    }

    FILE *fs = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (fs && status && status_len > 0) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), fs)) {
            char *nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
            snprintf(status, status_len, "%s", buf);
        }
        fclose(fs);
    }
}

static void get_power_profile(char *profile, size_t profile_len) {
    if (!profile || profile_len == 0) return;
    strncpy(profile, "balanced", profile_len);

    FILE *fp = popen("powerprofilesctl get 2>/dev/null", "r");
    if (fp) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), fp)) {
            char *nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
            if (strlen(buf) > 0) {
                strncpy(profile, buf, profile_len - 1);
                profile[profile_len - 1] = '\0';
            }
        }
        pclose(fp);
    }
}

static int set_power_profile(const char *profile) {
    if (!profile) return -1;
    if (strcmp(profile, "performance") != 0 &&
        strcmp(profile, "balanced") != 0 &&
        strcmp(profile, "power-saver") != 0) {
        return -1;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "powerprofilesctl set %s", profile);
    return system(cmd);
}

// -------------------------------------------------------------
// CLI Actions & JSON output
// -------------------------------------------------------------

static void print_status_json(ITEController *dev, int err_code) {
    bool is_on = true;
    uint8_t effect_id = 0x05;
    uint8_t speed = 5;
    uint8_t brightness = 25;
    uint8_t color_id = 8;

    bool dev_connected = false;
    bool perms_ok = false;

    if (err_code == 0 && dev && dev->handle) {
        dev_connected = true;
        perms_ok = true;
        ite_query_status(dev, &is_on, &effect_id, &speed, &brightness, &color_id);
    } else if (err_code == LIBUSB_ERROR_ACCESS) {
        dev_connected = true;
        perms_ok = false;
    }

    int cpu_temp = get_cpu_temp_celsius();
    int battery_pct = -1;
    char battery_status[32] = {0};
    get_battery_info(&battery_pct, battery_status, sizeof(battery_status));

    char power_profile[32] = {0};
    get_power_profile(power_profile, sizeof(power_profile));

    printf("{\n");
    printf("  \"laptop\": \"Avell A60 MUV\",\n");
    printf("  \"device\": {\n");
    printf("    \"connected\": %s,\n", dev_connected ? "true" : "false");
    printf("    \"permission_ok\": %s,\n", perms_ok ? "true" : "false");
    printf("    \"is_on\": %s,\n", is_on ? "true" : "false");
    printf("    \"brightness\": %u,\n", brightness);
    printf("    \"brightness_percent\": %d,\n", (int)((brightness * 100) / 50));
    printf("    \"speed\": %u,\n", speed);
    printf("    \"effect_id\": %u,\n", effect_id);
    printf("    \"effect_name\": \"%s\",\n", get_effect_name(effect_id));
    printf("    \"color_id\": %u\n", color_id);
    printf("  },\n");
    printf("  \"system\": {\n");
    printf("    \"cpu_temp_celsius\": %d,\n", cpu_temp);
    printf("    \"battery_percent\": %d,\n", battery_pct);
    printf("    \"battery_status\": \"%s\",\n", battery_status);
    printf("    \"power_profile\": \"%s\"\n", power_profile);
    printf("  }\n");
    printf("}\n");
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Uso: %s <comando> [opções]\n\n"
        "Comandos do Teclado RGB:\n"
        "  status [--json]      Exibe status atual em texto ou JSON\n"
        "  on                   Liga a iluminação do teclado\n"
        "  off                  Desliga a iluminação do teclado\n"
        "  brightness <0..50>   Ajusta o brilho (0 a 50)\n"
        "  color <hex|nome> [--brightness <0..50>] [--save]\n"
        "                       Define cor estática (ex: red, #ff5500, white)\n"
        "  effect <nome> [--speed <1..10>] [--brightness <0..50>] [--color <nome>] [--save]\n"
        "                       Aplica efeito dinâmico (rainbow, wave, breathing, etc.)\n"
        "  save                 Grava as configurações na memória ROM do teclado\n"
        "  check-perms          Verifica acesso ao dispositivo USB\n\n"
        "Comandos de Perfil do Sistema:\n"
        "  profile [performance|balanced|power-saver]\n"
        "                       Consulta ou define o perfil de energia\n\n"
        "Efeitos disponíveis:\n"
        "  breathing, wave, random, rainbow, ripple, marquee, raindrop, aurora, fireworks\n"
        , prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "check-perms") == 0) {
        ITEController dev;
        int r = ite_open(&dev);
        if (r == 0) {
            printf("OK: Dispositivo acessível e permissões configuradas.\n");
            ite_close(&dev);
            return 0;
        } else if (r == LIBUSB_ERROR_ACCESS) {
            fprintf(stderr, "ERRO: Permissão negada para 048d:ce00. Instale a regra udev.\n");
            return 2;
        } else {
            fprintf(stderr, "ERRO: Dispositivo não encontrado ou falha USB (código %d).\n", r);
            return 3;
        }
    }

    if (strcmp(cmd, "profile") == 0) {
        if (argc >= 3) {
            int ret = set_power_profile(argv[2]);
            if (ret == 0) {
                printf("Perfil de energia alterado para: %s\n", argv[2]);
                return 0;
            } else {
                fprintf(stderr, "Erro ao alterar perfil de energia para: %s\n", argv[2]);
                return 1;
            }
        } else {
            char profile[32] = {0};
            get_power_profile(profile, sizeof(profile));
            printf("%s\n", profile);
            return 0;
        }
    }

    if (strcmp(cmd, "status") == 0) {
        ITEController dev;
        int r = ite_open(&dev);
        print_status_json(&dev, r);
        if (r == 0) ite_close(&dev);
        return (r == 0 || r == LIBUSB_ERROR_ACCESS) ? 0 : 1;
    }

    // Commands below require open USB connection
    ITEController dev;
    int r = ite_open(&dev);
    if (r != 0) {
        if (r == LIBUSB_ERROR_ACCESS) {
            fprintf(stderr, "{\"error\": \"permission_denied\", \"message\": \"Acesso ao USB negado. Regra udev necessária.\"}\n");
            return 2;
        } else {
            fprintf(stderr, "{\"error\": \"no_device\", \"message\": \"Teclado ITE 8291 não encontrado.\"}\n");
            return 3;
        }
    }

    int exit_code = 0;

    if (strcmp(cmd, "off") == 0) {
        r = ite_turn_off(&dev);
        if (r >= 0) printf("{\"status\":\"success\",\"is_on\":false}\n");
        else exit_code = 1;
    }
    else if (strcmp(cmd, "on") == 0) {
        r = ite_turn_on(&dev);
        if (r >= 0) printf("{\"status\":\"success\",\"is_on\":true}\n");
        else exit_code = 1;
    }
    else if (strcmp(cmd, "brightness") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Informe o nível de brilho (0..50)\n");
            exit_code = 1;
        } else {
            int b = atoi(argv[2]);
            r = ite_set_brightness(&dev, (uint8_t)b);
            if (r >= 0) printf("{\"status\":\"success\",\"brightness\":%d}\n", b);
            else exit_code = 1;
        }
    }
    else if (strcmp(cmd, "color") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Informe a cor (hex, rgb ou nome)\n");
            exit_code = 1;
        } else {
            RGBColor color = {255, 255, 255};
            uint8_t code = 8;
            if (parse_color_name_or_hex(argv[2], &color, &code) != 0) {
                fprintf(stderr, "Cor inválida: %s\n", argv[2]);
                exit_code = 1;
            } else {
                uint8_t brightness = 25;
                bool save = false;

                // Read current brightness if possible
                uint8_t curr_b = 25;
                if (ite_query_status(&dev, NULL, NULL, NULL, &curr_b, NULL) == 0 && curr_b > 0) {
                    brightness = curr_b;
                }

                for (int i = 3; i < argc; i++) {
                    if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
                        brightness = (uint8_t)atoi(argv[++i]);
                    } else if (strcmp(argv[i], "--save") == 0) {
                        save = true;
                    }
                }

                r = ite_set_static_color(&dev, color, brightness, save);
                if (r >= 0) {
                    printf("{\"status\":\"success\",\"color\":\"%02x%02x%02x\",\"brightness\":%d}\n",
                           color.r, color.g, color.b, brightness);
                } else exit_code = 1;
            }
        }
    }
    else if (strcmp(cmd, "effect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Informe o nome do efeito\n");
            exit_code = 1;
        } else {
            uint8_t effect_id = parse_effect_name(argv[2]);
            if (effect_id == 0) {
                fprintf(stderr, "Efeito desconhecido: %s\n", argv[2]);
                exit_code = 1;
            } else {
                uint8_t speed = 5;
                uint8_t brightness = 25;
                uint8_t color = 8; // random / rainbow default
                uint8_t dir_reactive = 0;
                uint8_t save = 0;

                // default direction for wave is right (1)
                if (effect_id == 0x03) dir_reactive = 1;

                for (int i = 3; i < argc; i++) {
                    if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
                        speed = (uint8_t)atoi(argv[++i]);
                    } else if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
                        brightness = (uint8_t)atoi(argv[++i]);
                    } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
                        RGBColor dummy;
                        uint8_t c = 8;
                        if (parse_color_name_or_hex(argv[++i], &dummy, &c) == 0) {
                            color = c;
                        }
                    } else if (strcmp(argv[i], "--direction") == 0 && i + 1 < argc) {
                        const char *d = argv[++i];
                        if (strcasecmp(d, "right") == 0) dir_reactive = 1;
                        else if (strcasecmp(d, "left") == 0) dir_reactive = 2;
                        else if (strcasecmp(d, "up") == 0) dir_reactive = 3;
                        else if (strcasecmp(d, "down") == 0) dir_reactive = 4;
                    } else if (strcmp(argv[i], "--reactive") == 0) {
                        dir_reactive = 1;
                    } else if (strcmp(argv[i], "--save") == 0) {
                        save = 1;
                    }
                }

                r = ite_set_effect_raw(&dev, 0x02, effect_id, speed, brightness, color, dir_reactive, save);
                if (r >= 0) {
                    printf("{\"status\":\"success\",\"effect\":\"%s\",\"speed\":%d,\"brightness\":%d}\n",
                           argv[2], speed, brightness);
                } else exit_code = 1;
            }
        }
    }
    else if (strcmp(cmd, "save") == 0) {
        // Query current state and resend with save=1
        bool is_on = true;
        uint8_t eff = 0x05, spd = 5, br = 25, col = 8;
        if (ite_query_status(&dev, &is_on, &eff, &spd, &br, &col) == 0) {
            r = ite_set_effect_raw(&dev, is_on ? 0x02 : 0x01, eff, spd, br, col, 0, 1);
            if (r >= 0) printf("{\"status\":\"success\",\"saved\":true}\n");
            else exit_code = 1;
        } else {
            exit_code = 1;
        }
    }
    else {
        fprintf(stderr, "Comando desconhecido: %s\n", cmd);
        print_usage(argv[0]);
        exit_code = 1;
    }

    ite_close(&dev);
    return exit_code;
}
