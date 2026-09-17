/*
 * avell-ctl: Hardware control tool for Avell A60 MUV (ITE 8291 Rev 0.03)
 * Full AUCC protocol compatibility + libusb-1.0 native performance.
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
#include <fcntl.h>
#include <dirent.h>
#include <sys/file.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID   0x048D
#define PRODUCT_ID  0xCE00

#define ITE_INTERFACE       1
#define ITE_EP_OUT          0x02
#define ITE_EP_IN           0x81

#define CMD_SET_EFFECT      0x08
#define CMD_COLOR_SETUP     0x12
#define CMD_GET_EFFECT      0x88
#define CMD_GET_FW_VERSION  0x80

typedef struct {
    uint8_t r, g, b;
} RGBColor;

static const struct {
    const char *name;
    uint8_t code;
    uint8_t r, g, b;
} COLOR_TABLE[] = {
    { "red",        1, 0xFF, 0x00, 0x00 },
    { "orange",     2, 0xFF, 0x60, 0x00 },
    { "yellow",     3, 0xFF, 0xCC, 0x00 },
    { "green",      4, 0x00, 0xFF, 0x00 },
    { "blue",       5, 0x00, 0x00, 0xFF },
    { "teal",       6, 0x00, 0xFF, 0xFF },
    { "cyan",       6, 0x00, 0xFF, 0xFF },
    { "purple",     7, 0xCC, 0x00, 0xFF },
    { "pink",       7, 0xFF, 0x00, 0x77 },
    { "white",      8, 0xFF, 0xFF, 0xFF },
    { "gold",       2, 0xFF, 0xD7, 0x00 },
    { "random",     8, 0xFF, 0xFF, 0xFF },
    { "rainbow",    8, 0xFF, 0xFF, 0xFF },
    { NULL,         0,    0,    0,    0 }
};

typedef struct {
    const char *name;
    uint8_t program;
    uint8_t program2;
    uint8_t default_color;
} EffectDef;

static const EffectDef EFFECT_TABLE[] = {
    { "breathing",      0x02, 0x00, 0x08 },
    { "wave",           0x03, 0x01, 0x00 },
    { "random",         0x04, 0x00, 0x08 },
    { "reactive",       0x04, 0x01, 0x08 },
    { "rainbow",        0x05, 0x00, 0x00 },
    { "ripple",         0x06, 0x00, 0x08 },
    { "reactiveripple", 0x07, 0x00, 0x08 },
    { "marquee",        0x09, 0x00, 0x08 },
    { "raindrop",       0x0A, 0x00, 0x08 },
    { "aurora",         0x0E, 0x00, 0x08 },
    { "reactiveaurora", 0x0E, 0x01, 0x08 },
    { "fireworks",      0x11, 0x01, 0x08 },
    { NULL,             0x00, 0x00, 0x00 }
};

static uint8_t map_brightness(int b) {
    if (b <= 0) return 0x00;
    if (b <= 12) return 0x08;
    if (b <= 25) return 0x16;
    if (b <= 38) return 0x24;
    return 0x32; // max (50)
}

static int parse_color(const char *str, RGBColor *color, uint8_t *code_out) {
    if (!str || !color) return -1;

    if (str[0] == '#') str++;
    if (strlen(str) == 6) {
        unsigned int r, g, b;
        if (sscanf(str, "%02x%02x%02x", &r, &g, &b) == 3) {
            color->r = (uint8_t)r;
            color->g = (uint8_t)g;
            color->b = (uint8_t)b;
            if (code_out) *code_out = 8;
            return 0;
        }
    }

    unsigned int r, g, b;
    if (sscanf(str, "%u,%u,%u", &r, &g, &b) == 3) {
        color->r = (uint8_t)(r > 255 ? 255 : r);
        color->g = (uint8_t)(g > 255 ? 255 : g);
        color->b = (uint8_t)(b > 255 ? 255 : b);
        if (code_out) *code_out = 8;
        return 0;
    }

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

static const EffectDef* find_effect(const char *name) {
    if (!name) return NULL;
    for (int i = 0; EFFECT_TABLE[i].name != NULL; i++) {
        if (strcasecmp(EFFECT_TABLE[i].name, name) == 0) {
            return &EFFECT_TABLE[i];
        }
    }
    return NULL;
}

// -------------------------------------------------------------
// USB Device Controller
// -------------------------------------------------------------

typedef struct {
    libusb_context *ctx;
    libusb_device_handle *handle;
    bool claimed;
    int lock_fd;
} ITEController;

static int ite_open(ITEController *dev) {
    memset(dev, 0, sizeof(*dev));
    dev->lock_fd = -1;

    // File lock to prevent concurrent USB contention between UI and polling
    dev->lock_fd = open("/tmp/avell-ctl.lock", O_CREAT | O_RDWR, 0666);
    if (dev->lock_fd >= 0) {
        // Wait up to 1 second for lock
        for (int i = 0; i < 10; i++) {
            if (flock(dev->lock_fd, LOCK_EX | LOCK_NB) == 0) break;
            usleep(100000);
        }
    }

    int r = libusb_init(&dev->ctx);
    if (r < 0) {
        if (dev->lock_fd >= 0) close(dev->lock_fd);
        return r;
    }

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
        if (dev->lock_fd >= 0) close(dev->lock_fd);
        return LIBUSB_ERROR_NO_DEVICE;
    }

    r = libusb_open(target_device, &dev->handle);
    if (list) libusb_free_device_list(list, 1);

    if (r != 0 || !dev->handle) {
        libusb_exit(dev->ctx);
        dev->ctx = NULL;
        if (dev->lock_fd >= 0) close(dev->lock_fd);
        return (r != 0) ? r : LIBUSB_ERROR_ACCESS;
    }

    // Critical: Detach kernel driver (usbhid) from interface 1
    if (libusb_kernel_driver_active(dev->handle, ITE_INTERFACE) == 1) {
        libusb_detach_kernel_driver(dev->handle, ITE_INTERFACE);
    }

    r = libusb_claim_interface(dev->handle, ITE_INTERFACE);
    if (r < 0) {
        // Retry detach and claim
        libusb_detach_kernel_driver(dev->handle, ITE_INTERFACE);
        r = libusb_claim_interface(dev->handle, ITE_INTERFACE);
    }

    if (r < 0) {
        libusb_close(dev->handle);
        libusb_exit(dev->ctx);
        dev->handle = NULL;
        dev->ctx = NULL;
        if (dev->lock_fd >= 0) close(dev->lock_fd);
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
    if (dev->lock_fd >= 0) {
        flock(dev->lock_fd, LOCK_UN);
        close(dev->lock_fd);
        dev->lock_fd = -1;
    }
}

static int ite_send_ctrl(ITEController *dev, const uint8_t *payload, int len) {
    uint8_t buf[8] = {0};
    if (len > 8) len = 8;
    memcpy(buf, payload, len);

    return libusb_control_transfer(
        dev->handle,
        0x21, // bmRequestType: host to device, class, interface
        0x09, // bRequest: HID Set_Report
        0x0300, // wValue: Feature report
        0x0001, // wIndex: Interface 1
        buf,
        8,
        1500
    );
}

static int ite_get_ctrl(ITEController *dev, uint8_t *buf, int len) {
    return libusb_control_transfer(
        dev->handle,
        0xA1, // bmRequestType: device to host, class, interface
        0x01, // bRequest: HID Get_Report
        0x0300,
        0x0001,
        buf,
        len,
        1500
    );
}

static int ite_send_bulk(ITEController *dev, const uint8_t *data, int len) {
    int transferred = 0;
    return libusb_bulk_transfer(dev->handle, ITE_EP_OUT, (unsigned char*)data, len, &transferred, 1500);
}

// -------------------------------------------------------------
// AUCC Protocol Operations
// -------------------------------------------------------------

static int ite_disable(ITEController *dev) {
    uint8_t cmd[8] = { CMD_SET_EFFECT, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    return ite_send_ctrl(dev, cmd, 8);
}

static int ite_set_brightness_level(ITEController *dev, int brightness) {
    uint8_t b_code = map_brightness(brightness);
    uint8_t cmd[8] = { CMD_SET_EFFECT, 0x02, 0x33, 0x00, b_code, 0x00, 0x00, 0x00 };
    return ite_send_ctrl(dev, cmd, 8);
}

static int ite_set_color(ITEController *dev, RGBColor color, int brightness, bool save) {
    uint8_t b_code = map_brightness(brightness);

    // Step 1: Set brightness / user mode
    uint8_t b_cmd[8] = { CMD_SET_EFFECT, 0x02, 0x33, 0x00, b_code, 0x00, 0x00, 0x00 };
    int r = ite_send_ctrl(dev, b_cmd, 8);
    if (r < 0) return r;

    // Step 2: Color scheme setup
    uint8_t cs_cmd[8] = { CMD_COLOR_SETUP, 0x00, 0x00, 0x08, save ? 0x01 : 0x00, 0x00, 0x00, 0x00 };
    r = ite_send_ctrl(dev, cs_cmd, 8);
    if (r < 0) return r;

    // Step 3: AUCC 8x bulk transfer of 64 bytes (16 * [0x00, R, G, B])
    uint8_t payload[64];
    for (int i = 0; i < 16; i++) {
        payload[i*4 + 0] = 0x00;
        payload[i*4 + 1] = color.r;
        payload[i*4 + 2] = color.g;
        payload[i*4 + 3] = color.b;
    }

    for (int t = 0; t < 8; t++) {
        r = ite_send_bulk(dev, payload, 64);
        if (r < 0) return r;
    }

    return 0;
}

static int ite_set_effect(ITEController *dev, const EffectDef *eff, int speed, int brightness,
                         uint8_t color_code, uint8_t dir, bool save) {
    if (!eff) return -1;
    uint8_t b_code = map_brightness(brightness);
    uint8_t spd = (speed < 1) ? 1 : ((speed > 10) ? 10 : (uint8_t)speed);

    uint8_t program2 = eff->program2;
    uint8_t col = (color_code != 0) ? color_code : eff->default_color;

    // Direction handling for wave: right=1, left=2, up=3, down=4
    if (eff->program == 0x03 && dir != 0) {
        program2 = dir;
    }

    uint8_t cmd[8] = {
        CMD_SET_EFFECT,
        0x02,
        eff->program,
        spd,
        b_code,
        col,
        program2,
        save ? 0x01 : 0x00
    };

    return ite_send_ctrl(dev, cmd, 8);
}

static int ite_query(ITEController *dev, bool *is_on, uint8_t *effect_id,
                    uint8_t *speed, uint8_t *brightness, uint8_t *color_id) {
    uint8_t cmd[8] = { CMD_GET_EFFECT, 0, 0, 0, 0, 0, 0, 0 };
    int r = ite_send_ctrl(dev, cmd, 8);
    if (r < 0) return r;

    uint8_t resp[8] = {0};
    r = ite_get_ctrl(dev, resp, 8);
    if (r < 0) return r;

    if (is_on) *is_on = (resp[1] == 0x02);
    if (effect_id) *effect_id = resp[2];
    if (speed) *speed = resp[3];
    if (brightness) *brightness = resp[4];
    if (color_id) *color_id = resp[5];

    return 0;
}

// -------------------------------------------------------------
// System Sensors & Power
// -------------------------------------------------------------

static int get_cpu_temp(void) {
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

static void get_battery(int *cap, char *status, size_t status_len) {
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

static void get_profile(char *profile, size_t profile_len) {
    if (!profile || profile_len == 0) return;
    snprintf(profile, profile_len, "balanced");
    FILE *fp = popen("powerprofilesctl get 2>/dev/null", "r");
    if (fp) {
        char buf[64] = {0};
        if (fgets(buf, sizeof(buf), fp)) {
            char *nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
            if (strlen(buf) > 0) {
                snprintf(profile, profile_len, "%s", buf);
            }
        }
        pclose(fp);
    }
}

static int set_profile(const char *p) {
    if (!p) return -1;
    if (strcmp(p, "performance") != 0 &&
        strcmp(p, "balanced") != 0 &&
        strcmp(p, "power-saver") != 0) return -1;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "powerprofilesctl set %s", p);
    return system(cmd);
}

// -------------------------------------------------------------
// Status Output
// -------------------------------------------------------------

static void print_status(ITEController *dev, int err_code) {
    bool is_on = true;
    uint8_t effect_id = 0x05;
    uint8_t speed = 5;
    uint8_t brightness = 0x24; // 36
    uint8_t color_id = 8;

    bool dev_connected = false;
    bool perms_ok = false;

    if (err_code == 0 && dev && dev->handle) {
        dev_connected = true;
        perms_ok = true;
        ite_query(dev, &is_on, &effect_id, &speed, &brightness, &color_id);
    } else if (err_code == LIBUSB_ERROR_ACCESS) {
        dev_connected = true;
        perms_ok = false;
    }

    int cpu_temp = get_cpu_temp();
    int battery_pct = -1;
    char battery_status[32] = {0};
    get_battery(&battery_pct, battery_status, sizeof(battery_status));

    char power_profile[32] = {0};
    get_profile(power_profile, sizeof(power_profile));

    const char *effect_name = "rainbow";
    for (int i = 0; EFFECT_TABLE[i].name != NULL; i++) {
        if (EFFECT_TABLE[i].program == effect_id) {
            effect_name = EFFECT_TABLE[i].name;
            break;
        }
    }
    if (effect_id == 0x33) effect_name = "user";

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
    printf("    \"effect_name\": \"%s\",\n", effect_name);
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

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: %s [status|on|off|brightness|color|effect|save|profile|check-perms]\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "profile") == 0) {
        if (argc >= 3) {
            return set_profile(argv[2]);
        } else {
            char profile[32] = {0};
            get_profile(profile, sizeof(profile));
            printf("%s\n", profile);
            return 0;
        }
    }

    if (strcmp(cmd, "check-perms") == 0) {
        ITEController dev;
        int r = ite_open(&dev);
        if (r == 0) {
            printf("OK: Dispositivo acessível.\n");
            ite_close(&dev);
            return 0;
        } else if (r == LIBUSB_ERROR_ACCESS) {
            fprintf(stderr, "ERRO: Permissão negada para 048d:ce00. Instale a regra udev.\n");
            return 2;
        } else {
            fprintf(stderr, "ERRO: Falha ao abrir dispositivo (código %d: %s).\n", r, libusb_error_name(r));
            return 3;
        }
    }

    if (strcmp(cmd, "status") == 0) {
        ITEController dev;
        int r = ite_open(&dev);
        print_status(&dev, r);
        if (r == 0) ite_close(&dev);
        return 0;
    }

    // USB write commands
    ITEController dev;
    int r = ite_open(&dev);
    if (r != 0) {
        if (r == LIBUSB_ERROR_ACCESS) {
            fprintf(stderr, "{\"error\":\"permission_denied\",\"message\":\"Acesso ao USB negado.\"}\n");
            return 2;
        } else {
            fprintf(stderr, "{\"error\":\"usb_error\",\"code\":%d,\"message\":\"%s\"}\n", r, libusb_error_name(r));
            return 3;
        }
    }

    int exit_code = 0;

    if (strcmp(cmd, "off") == 0) {
        r = ite_disable(&dev);
        if (r >= 0) printf("{\"status\":\"success\",\"is_on\":false}\n");
        else exit_code = 1;
    }
    else if (strcmp(cmd, "on") == 0) {
        // Restore rainbow by default
        const EffectDef *rainbow = find_effect("rainbow");
        r = ite_set_effect(&dev, rainbow, 5, 36, 0, 0, false);
        if (r >= 0) printf("{\"status\":\"success\",\"is_on\":true}\n");
        else exit_code = 1;
    }
    else if (strcmp(cmd, "brightness") == 0) {
        if (argc < 3) {
            exit_code = 1;
        } else {
            int b = atoi(argv[2]);
            r = ite_set_brightness_level(&dev, b);
            if (r >= 0) printf("{\"status\":\"success\",\"brightness\":%d}\n", b);
            else exit_code = 1;
        }
    }
    else if (strcmp(cmd, "color") == 0) {
        if (argc < 3) {
            exit_code = 1;
        } else {
            RGBColor color = {0, 255, 0};
            uint8_t code = 8;
            if (parse_color(argv[2], &color, &code) != 0) {
                fprintf(stderr, "Cor inválida: %s\n", argv[2]);
                exit_code = 1;
            } else {
                int brightness = 36;
                bool save = false;
                for (int i = 3; i < argc; i++) {
                    if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
                        brightness = atoi(argv[++i]);
                    } else if (strcmp(argv[i], "--save") == 0) {
                        save = true;
                    }
                }
                r = ite_set_color(&dev, color, brightness, save);
                if (r >= 0) {
                    printf("{\"status\":\"success\",\"color\":\"%02x%02x%02x\"}\n",
                           color.r, color.g, color.b);
                } else exit_code = 1;
            }
        }
    }
    else if (strcmp(cmd, "effect") == 0) {
        if (argc < 3) {
            exit_code = 1;
        } else {
            const EffectDef *eff = find_effect(argv[2]);
            if (!eff) {
                fprintf(stderr, "Efeito desconhecido: %s\n", argv[2]);
                exit_code = 1;
            } else {
                int speed = 5;
                int brightness = 36;
                uint8_t col_code = 0;
                uint8_t dir = 0;
                bool save = false;

                for (int i = 3; i < argc; i++) {
                    if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
                        speed = atoi(argv[++i]);
                    } else if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
                        brightness = atoi(argv[++i]);
                    } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
                        RGBColor dummy;
                        parse_color(argv[++i], &dummy, &col_code);
                    } else if (strcmp(argv[i], "--direction") == 0 && i + 1 < argc) {
                        const char *d = argv[++i];
                        if (strcasecmp(d, "right") == 0) dir = 1;
                        else if (strcasecmp(d, "left") == 0) dir = 2;
                        else if (strcasecmp(d, "up") == 0) dir = 3;
                        else if (strcasecmp(d, "down") == 0) dir = 4;
                    } else if (strcmp(argv[i], "--save") == 0) {
                        save = true;
                    }
                }

                r = ite_set_effect(&dev, eff, speed, brightness, col_code, dir, save);
                if (r >= 0) {
                    printf("{\"status\":\"success\",\"effect\":\"%s\"}\n", argv[2]);
                } else exit_code = 1;
            }
        }
    }
    else if (strcmp(cmd, "save") == 0) {
        // AUCC save: trigger color_scheme_setup with save=1
        uint8_t cs_cmd[8] = { CMD_COLOR_SETUP, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00 };
        r = ite_send_ctrl(&dev, cs_cmd, 8);
        if (r >= 0) printf("{\"status\":\"success\",\"saved\":true}\n");
        else exit_code = 1;
    }
    else {
        fprintf(stderr, "Comando desconhecido: %s\n", cmd);
        exit_code = 1;
    }

    ite_close(&dev);
    return exit_code;
}
