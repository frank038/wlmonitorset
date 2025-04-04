#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "str_vec.h"

#define WLMONITORSET_VERSION "0.5"
#define MAX_STRING (256*23)

int set_timer2(struct itimerspec timerspec,int t);
struct itimerspec set_struct2(void);
struct rgb calc_whitepoint2(int temp);
int time_to_today(int d);
time_t date_time_today(void);
int f_time_to_add(int sunrise, int sunset, int dusk, int *t, int *d);
int get_data_array(void);
float rgbbright = 1.0;
float sunrisebright = 1.0;
float sunsetbright = 1.0;
float duskbright = 1.0;
int calculate_bright(char *s, float *bsunrise, float *bsunset, float *bdusk);

int icfile = 0; // for icc like setting: 1 or 2
char myicfile[] = "data_array";

char temp[3][256][22]; // 256 values per colour channel
//float temp2[3][256]; // 256 values per colour channel
int get_data_array(void) {
    
    FILE *fp = fopen("data_array","r");
    if (fp == NULL) {
        return -1;
    }
    
    char cc[3][MAX_STRING];
    int ii = 0;
    while ( (fgets(cc[ii],MAX_STRING,fp) != NULL) ) {
        ++ii;
    }
    
    fclose(fp);
    
    char *ttemp;
    for (int i=0;i<3;++i) {
        int j = 0;
        ttemp = strtok(cc[i]," ");
        if (ttemp != NULL) {
            strcpy(temp[i][j],ttemp);
            //temp2[i][j] = atof(ttemp);
        }
        ++j;
        while ( (ttemp = strtok(NULL," ")) != NULL ) {
            if ( ttemp[strlen(ttemp)-1] == '\n' ) {
                ttemp[strlen(ttemp)-1] = '\0';
            }
            strcpy(temp[i][j],ttemp);
            //temp2[i][j] = atof(ttemp);
            ++j;
        }
    }
    return 0;
}

int what_cal = 0; // 0 no

struct config {
    int high_temp;
    int low_temp;
    int dusk_temp;
    double gamma;

    bool manual_time;
    time_t sunrise;
    time_t sunset;
    time_t dusk;
    time_t duration;

    struct str_vec output_names;
};

enum state {
    STATE_INITIAL,
    STATE_NORMAL,
    STATE_TRANSITION,
    STATE_STATIC,
    STATE_FORCED,
};

enum force_state {
    FORCE_OFF,
    FORCE_HIGH,
    FORCE_LOW,
};

struct sun {
    time_t dawn;
    time_t sunrise;
    time_t sunset;
    time_t dusk;
};

enum sun_condition {
    NORMAL,
    MIDNIGHT_SUN,
    POLAR_NIGHT,
    SUN_CONDITION_LAST
};

struct rgb {
  double r, g, b;
};

struct context {
    struct config config;
    struct sun sun;

    enum state state;
    enum sun_condition condition;

    time_t dawn_step_time;
    time_t dusk_step_time;
    time_t calc_day;

    bool new_output;
    struct wl_list outputs;
    timer_t timer;

    enum force_state forced_state;

    struct zwlr_gamma_control_manager_v1 *gamma_control_manager;
};

struct output {
    struct wl_list link;

    struct context *context;
    struct wl_output *wl_output;
    struct zwlr_gamma_control_v1 *gamma_control;

    int table_fd;
    uint32_t id;
    uint32_t ramp_size;
    uint16_t *table;
    bool enabled;
    char *name;
};


static int create_anonymous_file(off_t size) {
    char template[] = "/tmp/wlsunset-shared-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        return -1;
    }

    int ret;
    do {
        errno = 0;
        ret = ftruncate(fd, size);
    } while (errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }

    unlink(template);
    return fd;
}

static int create_gamma_table(uint32_t ramp_size, uint16_t **table) {
    size_t table_size = ramp_size * 3 * sizeof(uint16_t);
    int fd = create_anonymous_file(table_size);
    if (fd < 0) {
        fprintf(stderr, "failed to create anonymous file\n");
        return -1;
    }

    void *data =
        mmap(NULL, table_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "failed to mmap()\n");
        close(fd);
        return -1;
    }

    *table = data;
    return fd;
}

static void gamma_control_handle_gamma_size(void *data,
        struct zwlr_gamma_control_v1 *gamma_control, uint32_t ramp_size) {
    (void)gamma_control;
    struct output *output = data;
    if (output->table_fd != -1) {
        close(output->table_fd);
        output->table_fd = -1;
    }
    output->ramp_size = ramp_size;
    if (ramp_size == 0) {
        // Maybe the output does not currently have a CRTC to tell us
        // the gamma size, let's clean up and retry on next set.
        zwlr_gamma_control_v1_destroy(output->gamma_control);
        output->gamma_control = NULL;
        return;
    }
    output->table_fd = create_gamma_table(ramp_size, &output->table);
    output->context->new_output = true;
    if (output->table_fd < 0) {
        fprintf(stderr, "could not create gamma table for output %s (%d)\n",
                output->name, output->id);
        exit(EXIT_FAILURE);
    }
}

static void gamma_control_handle_failed(void *data,
        struct zwlr_gamma_control_v1 *gamma_control) {
    (void)gamma_control;
    struct output *output = data;
    fprintf(stderr, "gamma control of output %s (%d) failed\n",
            output->name, output->id);
    zwlr_gamma_control_v1_destroy(output->gamma_control);
    output->gamma_control = NULL;
    if (output->table_fd != -1) {
        close(output->table_fd);
        output->table_fd = -1;
    }
}

static const struct zwlr_gamma_control_v1_listener gamma_control_listener = {
    .gamma_size = gamma_control_handle_gamma_size,
    .failed = gamma_control_handle_failed,
};

static void setup_gamma_control(struct context *ctx, struct output *output) {
    if (output->gamma_control != NULL) {
        return;
    }
    if (ctx->gamma_control_manager == NULL) {
        fprintf(stderr, "skipping setup of output %s (%d): gamma_control_manager missing\n",
                output->name, output->id);
        return;
    }
    output->gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(
        ctx->gamma_control_manager, output->wl_output);
    zwlr_gamma_control_v1_add_listener(output->gamma_control,
        &gamma_control_listener, output);
}

static void wl_output_handle_geometry(void *data, struct wl_output *output, int x, int y, int width,
                      int height, int subpixel, const char *make, const char *model,
                      int transform) {
    (void)data, (void)output, (void)x, (void)y, (void)width, (void)height, (void)subpixel,
        (void)make, (void)model, (void)transform;
}

static void wl_output_handle_mode(void *data, struct wl_output *output, uint32_t flags, int width,
                  int height, int refresh) {
    (void)data, (void)output, (void)flags, (void)width, (void)height, (void)refresh;
}

static void wl_output_handle_done(void *data, struct wl_output *wl_output) {
    (void)wl_output;
    struct output *output = data;
    setup_gamma_control(output->context, output);
}

static void wl_output_handle_scale(void *data, struct wl_output *output, int scale) {
    (void)data, (void)output, (void)scale;
}

static void wl_output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)wl_output;
    struct output *output = data;
    output->name = strdup(name);
    struct config *cfg = &output->context->config;
    for (size_t idx = 0; idx < cfg->output_names.len; ++idx) {
        if (strcmp(output->name, cfg->output_names.data[idx]) == 0) {
            fprintf(stderr, "enabling output %s by name\n", output->name);
            output->enabled = true;
            return;
        }
    }
}

static void wl_output_handle_description(void *data, struct wl_output *wl_output, const char *description) {
    (void)wl_output;
    struct output *output = data;
    struct config *cfg = &output->context->config;
    for (size_t idx = 0; idx < cfg->output_names.len; ++idx) {
        if (strcmp(description, cfg->output_names.data[idx]) == 0) {
            fprintf(stderr, "enabling output %s by description\n", description);
            output->enabled = true;
            return;
        }
    }
}

struct wl_output_listener wl_output_listener = {
    .geometry = wl_output_handle_geometry,
    .mode = wl_output_handle_mode,
    .done = wl_output_handle_done,
    .scale = wl_output_handle_scale,
    .name = wl_output_handle_name,
    .description = wl_output_handle_description,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    struct context *ctx = (struct context *)data;
    if (strcmp(interface, wl_output_interface.name) == 0) {
        fprintf(stderr, "registry: adding output %d\n", name);

        struct output *output = calloc(1, sizeof(struct output));
        output->id = name;
        output->table_fd = -1;
        output->context = ctx;

        if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
            output->enabled = ctx->config.output_names.len == 0;
            output->wl_output = wl_registry_bind(registry, name,
                    &wl_output_interface, WL_OUTPUT_NAME_SINCE_VERSION);
            wl_output_add_listener(output->wl_output, &wl_output_listener, output);
        } else {
            fprintf(stderr, "wl_output: old version (%d < %d), disabling name support\n",
                    version, WL_OUTPUT_NAME_SINCE_VERSION);
            output->enabled = true;
            output->wl_output = wl_registry_bind(registry, name,
                    &wl_output_interface, version);
            setup_gamma_control(ctx, output);
        }

        wl_list_insert(&ctx->outputs, &output->link);
    } else if (strcmp(interface,
                zwlr_gamma_control_manager_v1_interface.name) == 0) {
        ctx->gamma_control_manager = wl_registry_bind(registry, name,
                &zwlr_gamma_control_manager_v1_interface, 1);
    }
}

static void registry_handle_global_remove(void *data,
        struct wl_registry *registry, uint32_t name) {
    (void)registry;
    struct context *ctx = (struct context *)data;
    struct output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &ctx->outputs, link) {
        if (output->id == name) {
            fprintf(stderr, "registry: removing output %s (%d)\n", output->name, name);
            free(output->name);
            wl_list_remove(&output->link);
            if (output->gamma_control != NULL) {
                zwlr_gamma_control_v1_destroy(output->gamma_control);
            }
            if (output->table_fd != -1) {
                close(output->table_fd);
            }
            free(output);
            break;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};


static void fill_gamma_table(uint16_t *table, uint32_t ramp_size, double rw,
        double gw, double bw, double gamma, float now_bright) {
    uint16_t *r = table;
    uint16_t *g = table + ramp_size;
    uint16_t *b = table + 2 * ramp_size;
    for (uint32_t i = 0; i < ramp_size; ++i) {
        double val = (double)i / (ramp_size - 1);
        r[i] = (uint16_t)(UINT16_MAX * pow(val * rw, 1.0 / gamma) * now_bright);
        g[i] = (uint16_t)(UINT16_MAX * pow(val * gw, 1.0 / gamma) * now_bright);
        b[i] = (uint16_t)(UINT16_MAX * pow(val * bw, 1.0 / gamma) * now_bright);
    }
}


// linear interpolation
static void fill_gamma_table0(uint16_t *table, uint32_t ramp_size, double rrw,
                            double ggw, double bbw, float now_bright) {
    uint16_t *r = table;
    uint16_t *g = table + ramp_size;
    uint16_t *b = table + 2 * ramp_size;
    
    int step_ramp_size = (ramp_size/256);
    int mod_ramp_size = (ramp_size%256);
    
    float rw = 0.0;
    float gw = 0.0;
    float bw = 0.0;
    
    float rw2 = 0.0;
    float gw2 = 0.0;
    float bw2 = 0.0;
    
    float rwt = 0.0;
    float gwt = 0.0;
    float bwt = 0.0;
    
    int a;
    // spare values
    for (a=0;a<mod_ramp_size;++a) {
        r[a] = (uint16_t)(UINT16_MAX * 0 * rrw * now_bright);
        g[a] = (uint16_t)(UINT16_MAX * 0 * ggw * now_bright);
        b[a] = (uint16_t)(UINT16_MAX * 0 * bbw * now_bright);
    }
    
    int j = 0; // total fields from data_array: 256 - 0 to 255
    for (uint32_t i = a; i < ramp_size; i += step_ramp_size) {
        // initial values of each step
        rw = atof(temp[0][j]);
        gw = atof(temp[1][j]);
        bw = atof(temp[2][j]);
        
        if (j < 255) {
            // next data in data_array
            rw2 = atof(temp[0][j+1]);
            gw2 = atof(temp[1][j+1]);
            bw2 = atof(temp[2][j+1]);
            // interpolation (for the whole lenght of the step)
            rwt = (rw2-rw)/step_ramp_size;
            gwt = (gw2-gw)/step_ramp_size;
            bwt = (bw2-bw)/step_ramp_size;
        } else if (j == 255) {
            rwt = 0.0;
            gwt = 0.0;
            bwt = 0.0;
        }
        
        for (int jj=0;jj<step_ramp_size;++jj) {
            // i is constant in this cicle while jj vary
            r[i+jj] = (uint16_t)(UINT16_MAX * (rw+rwt*jj) * rrw * now_bright);
            g[i+jj] = (uint16_t)(UINT16_MAX * (gw+gwt*jj) * ggw * now_bright);
            b[i+jj] = (uint16_t)(UINT16_MAX * (bw+bwt*jj) * bbw * now_bright);
        }
        ++j;
    }
} 


// previous behaviour
static void fill_gamma_table3(uint16_t *table, uint32_t ramp_size, double rrw,
                            double ggw, double bbw, float now_bright) {
    uint16_t *r = table;
    uint16_t *g = table + ramp_size;
    uint16_t *b = table + 2 * ramp_size;
    
    int step_ramp_size = (ramp_size/256);
    int mod_ramp_size = (ramp_size%256);
    
    float rw = 0.0;
    float gw = 0.0;
    float bw = 0.0;
    
    int a;
    for (a=0;a<mod_ramp_size;a++) {
        r[a] = (uint16_t)(UINT16_MAX * 0 * rrw * now_bright);
        g[a] = (uint16_t)(UINT16_MAX * 0 * ggw * now_bright);
        b[a] = (uint16_t)(UINT16_MAX * 0 * bbw * now_bright);
    }
    
    int j = 0; // total fields: 256
    for (uint32_t i = a; i < ramp_size; ) {
        rw = atof(temp[0][j]);
        gw = atof(temp[1][j]);
        bw = atof(temp[2][j]);
        
        for (int jj=0;jj<step_ramp_size;++jj) {
            
            r[i] = (uint16_t)(UINT16_MAX * rw * rrw * now_bright);
            g[i] = (uint16_t)(UINT16_MAX * gw * ggw * now_bright);
            b[i] = (uint16_t)(UINT16_MAX * bw * bbw * now_bright);
            
            ++i;
        }
        ++j;
    }
} 

static void output_set_whitepoint(struct output *output, struct rgb *wp, double gamma, float now_bright) {
    if (!output->enabled || output->gamma_control == NULL || output->table_fd == -1) {
        return;
    }
    if ( what_cal != 0 && icfile == 1 ) { // linear interpolation
        fill_gamma_table0(output->table, output->ramp_size, wp->r, wp->g, wp->b, now_bright);
    } else if ( what_cal != 0 && icfile == 2 ) { // old method
        fill_gamma_table3(output->table, output->ramp_size, wp->r, wp->g, wp->b, now_bright);
    } else if (what_cal != 0) { // no -f option
        fill_gamma_table(output->table, output->ramp_size, wp->r, wp->g, wp->b, gamma, now_bright);
    }
    lseek(output->table_fd, 0, SEEK_SET);
    zwlr_gamma_control_v1_set_gamma(output->gamma_control,
            output->table_fd);
}

float mytemp[7][3] = {
    {1.0000,0.4232,0.0000}, // 1500
    {1.0000,0.6437,0.2881}, // 2500
    {1.0000,0.7798,0.5464}, // 3500
    {1.0000,0.8686,0.7368}, // 4500
    {1.0000,0.9385,0.8813}, // 5500
    {1.0000,1.0000,1.0000}, // 6500
    {0.8278,0.8901,1.0000}, // 9000
    };

struct rgb calc_whitepoint2(int temp) {
    if (temp == 6500) {
        return (struct rgb) {.r = 1.0, .g = 1.0, .b = 1.0};
    } else if (temp == 5500) {
        return (struct rgb) {.r = mytemp[4][0], .g = mytemp[4][1], .b = mytemp[4][2]};
    } else if (temp == 4500) {
        return (struct rgb) {.r = mytemp[3][0], .g = mytemp[3][1], .b = mytemp[3][2]};
    } else if (temp == 3500) {
        return (struct rgb) {.r = mytemp[2][0], .g = mytemp[2][1], .b = mytemp[2][2]};
    } else if (temp == 2500) {
        return (struct rgb) {.r = mytemp[1][0], .g = mytemp[1][1], .b = mytemp[1][2]};
    } else if (temp == 1500) {
        return (struct rgb) {.r = mytemp[0][0], .g = mytemp[0][1], .b = mytemp[0][2]};
    } else if (temp == 9000) {
        return (struct rgb) {.r = mytemp[6][0], .g = mytemp[6][1], .b = mytemp[6][2]};
    } else if (temp < 6500) {
        float rrr;
        float ggg;
        float bbb;
        float rr;
        float gg;
        float bb;
        int ff;
        
        if (temp < 6500 && temp > 5500) {
            rrr = mytemp[5][0];
            ggg = mytemp[5][1];
            bbb = mytemp[5][2];
            rr = mytemp[4][0];
            gg = mytemp[4][1];
            bb = mytemp[4][2];
            ff = 6500;
        } else if (temp < 5500 && temp > 4500) {
            rrr = mytemp[4][0];
            ggg = mytemp[4][1];
            bbb = mytemp[4][2];
            rr = mytemp[3][0];
            gg = mytemp[3][1];
            bb = mytemp[3][2];
            ff = 5500;
        } else if (temp < 4500 && temp > 3500) {
            rrr = mytemp[3][0];
            ggg = mytemp[3][1];
            bbb = mytemp[3][2];
            rr = mytemp[2][0];
            gg = mytemp[2][1];
            bb = mytemp[2][2];
            ff = 4500;
        } else if (temp < 3500 && temp > 2500) {
            rrr = mytemp[2][0];
            ggg = mytemp[2][1];
            bbb = mytemp[2][2];
            rr = mytemp[1][0];
            gg = mytemp[1][1];
            bb = mytemp[1][2];
            ff = 3500;
        } else if (temp < 2500 && temp > 1500) {
            rrr = mytemp[1][0];
            ggg = mytemp[1][1];
            bbb = mytemp[1][2];
            rr = mytemp[0][0];
            gg = mytemp[0][1];
            bb = mytemp[0][2];
            ff = 2500;
        }
        
        float ar = (rrr-rr)/1000;
        float ag = (ggg-gg)/1000;
        float ab = (bbb-bb)/1000;
        
        float fr = rrr-(ff-temp)*ar;
        float fg = ggg-(ff-temp)*ag;
        float fb = bbb-(ff-temp)*ab;
        
        return (struct rgb) {.r = fr, .g = fg, .b = fb};
    } else if ( temp == 9000 ){
        return (struct rgb) {.r = mytemp[6][0], .g = mytemp[6][1], .b = mytemp[6][2]};
    } else if (temp > 6500 && temp < 9000) {
        float rrr;
        float ggg;
        float bbb;
        float rr;
        float gg;
        float bb;
        int ff = 6500;
        
        rrr = mytemp[5][0];
        ggg = mytemp[5][1];
        bbb = mytemp[5][2];
        rr = mytemp[6][0];
        gg = mytemp[6][1];
        bb = mytemp[6][2];
        
        float ar = (rrr-rr)/2500;
        float ag = (ggg-gg)/2500;
        float ab = (bbb-bb)/2500;
        
        float fr = rrr+(ff-temp)*ar;
        float fg = ggg+(ff-temp)*ag;
        float fb = bbb+(ff-temp)*ab;
        
        return (struct rgb) {.r = fr, .g = fg, .b = fb};
    } else {
        printf("Temperature not supported: %d.\n", temp);
        return (struct rgb) {.r = 1.0, .g = 1.0, .b = 1.0};
    }
}


timer_t mytimer;
static void set_temperature(struct wl_list *outputs, int temp, double gamma, float now_bright) {
    struct output *output;
    struct rgb wp = calc_whitepoint2(temp);
    wl_list_for_each(output, outputs, link) {
        if (output->gamma_control == NULL) {
            setup_gamma_control(output->context, output);
            continue;
        }
        output_set_whitepoint(output, &wp, gamma, now_bright);
    }
}

static int timer_fired = 0;
static int usr1_fired = 0;
static int signal_fds[2];

static int display_dispatch(struct wl_display *display, int timeout) {
    if (wl_display_prepare_read(display) == -1) {
        return wl_display_dispatch_pending(display);
    }
    struct pollfd pfd[2];
    pfd[0].fd = wl_display_get_fd(display);
    pfd[1].fd = signal_fds[0];

    pfd[0].events = POLLOUT;
    // If we hit EPIPE we might have hit a protocol error. Continue reading
    // so that we can see what happened.
    while (wl_display_flush(display) == -1 && errno != EPIPE) {
        if (errno != EAGAIN) {
            wl_display_cancel_read(display);
            return -1;
        }
        // We only poll the wayland fd here
        while (poll(pfd, 1, timeout) == -1) {
            if (errno != EINTR) {
                wl_display_cancel_read(display);
                return -1;
            }
        }
    }
    pfd[0].events = POLLIN;
    pfd[1].events = POLLIN;
    while (poll(pfd, 2, timeout) == -1) {
        if (errno != EINTR) {
            wl_display_cancel_read(display);
            return -1;
        }
    }
    if (pfd[1].revents & POLLIN) {
        // Empty signal fd
        int signal;
        int res = read(signal_fds[0], &signal, sizeof signal);
        if (res == -1) {
            if (errno != EAGAIN) {
                return -1;
            }
        } else if (res != 4) {
            fprintf(stderr, "could not read full signal ID\n");
            return -1;
        }
        switch (signal) {
        case SIGALRM:
            timer_fired = true;
            break;
        case SIGUSR1:
            // do something
            usr1_fired = true;
            break;
        }
    }
    if ((pfd[0].revents & POLLIN) == 0) {
        wl_display_cancel_read(display);
        return 0;
    }

    if (wl_display_read_events(display) == -1) {
        return -1;
    }
    return wl_display_dispatch_pending(display);
}

static void signal_handler(int signal) {
    if (write(signal_fds[1], &signal, sizeof signal) == -1 && errno != EAGAIN) {
        // This is unfortunate.
    }
}

static int set_nonblock(int fd) {
    int flags;
    if ((flags = fcntl(fd, F_GETFL)) == -1 ||
            fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }
    return 0;
}

static int setup_signals(struct context *ctx) {
    struct sigaction signal_action = {
        .sa_handler = signal_handler,
        .sa_flags = 0,
    };
    if (pipe(signal_fds) == -1) {
        fprintf(stderr, "could not create signal pipe: %s\n",
                strerror(errno));
        return -1;
    }
    if (set_nonblock(signal_fds[0]) == -1 ||
            set_nonblock(signal_fds[1]) == -1) {
        fprintf(stderr, "could not set nonblock on signal pipe: %s\n",
                strerror(errno));
        return -1;
    }
    if (sigaction(SIGALRM, &signal_action, NULL) == -1) {
        fprintf(stderr, "could not configure SIGALRM handler: %s\n",
                strerror(errno));
        return -1;
    }
    if (sigaction(SIGUSR1, &signal_action, NULL) == -1) {
        fprintf(stderr, "could not configure SIGUSR1 handler: %s\n",
                strerror(errno));
        return -1;
    }
    if (timer_create(CLOCK_REALTIME, NULL, &ctx->timer) == -1) {
        fprintf(stderr, "could not configure timer: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

// HH and MM in unix time from 00:00 of the time; today ro tomorrow
static int parse_time_of_day(const char *s, time_t *time) {
    struct tm tm = { 0 };
    if (strptime(s, "%H:%M", &tm) == NULL) {
        return -1;
    }
    *time = tm.tm_hour * 3600 + tm.tm_min * 60;
    return 0;
}


int calculate_bright(char *s, float *bsunrise, float *bsunset, float *bdusk) {
    int i = 0;
    char *ttemp;
    if ((ttemp = strtok(s,":")) != NULL ) {
        *bsunrise = strtof(ttemp, NULL);
        ++i;
    }
    if ((ttemp = strtok(NULL,":")) != NULL ) {
        *bsunset = strtof(ttemp, NULL);
        ++i;
    }
    if ((ttemp = strtok(NULL,":")) != NULL ) {
        *bdusk = strtof(ttemp, NULL);
        ++i;
    }
    return i;
}

struct itimerspec set_struct2(void) {
    struct itimerspec timerspec;
    memset(&timerspec, 0, sizeof(struct itimerspec));
    timer_create(CLOCK_REALTIME, NULL, &mytimer);
    timerspec.it_value.tv_sec = 0;
    return timerspec;
}

int set_timer2(struct itimerspec timerspec,int next_time) {
    timerspec.it_value.tv_sec = next_time;
    timer_settime(mytimer, TIMER_ABSTIME, &timerspec, NULL);
    //printf("Set timer next time: %s\n", ctime(&timerspec.it_value.tv_sec));
    return 0;
}

time_t start_time = 0;
// unix time until 00:00 of today
int time_to_today(int dd) {
    time_t rawtime;
    struct tm * timeinfo;
    int hour = 0, min = 0, sec = 0;
    
    /* get current timeinfo: */
    time ( &rawtime );
    /* convert to struct: */
    timeinfo = localtime ( &rawtime );
    timeinfo->tm_mday   += dd;
    timeinfo->tm_hour   = hour;         //hours since midnight - [0,23]
    timeinfo->tm_min    = min;          //minutes after the hour - [0,59]
    timeinfo->tm_sec    = sec;          //seconds after the minute - [0,59]

    ///* call mktime: create unix time stamp from timeinfo struct */
    int date = mktime ( timeinfo );
    return date;
}

// return now in unix time
time_t date_time_today(void) {
    struct timespec realtime;
    clock_gettime(CLOCK_REALTIME, &realtime);
    time_t now_time = realtime.tv_sec;
    return now_time;
}

int f_time_to_add(int sunrise, int sunset, int dusk, int *time_to_add, int *what_cal) {
    // unix time until 00:00 of the day
    int unix_time_to_prev_midnight = time_to_today(0);
    const int time_of_the_day = (24*60*60); // 86400
    time_t now_time = date_time_today();
    // check and set the day and time for the next colour setting
    int c_r = sunrise;
    int c_s = sunset;
    int c_d = dusk;
    
    if (c_d > 0 && c_d > time_of_the_day) { // tomorrow
        if (now_time > unix_time_to_prev_midnight+c_s && c_s < time_of_the_day) { // today
            *time_to_add = unix_time_to_prev_midnight+c_d;
            // now is sunset
            *what_cal = 2;
        } else { // today
            *time_to_add = unix_time_to_prev_midnight+c_s;
            // now is sunrise
            *what_cal = 1;
        }
    } else if (c_d > 0) { // today
        if (now_time > unix_time_to_prev_midnight+c_d) {
            *time_to_add = unix_time_to_prev_midnight+c_r+(24*60*60); // tomorrow
            // now is dusk
            *what_cal = 3;
        } else if (now_time > unix_time_to_prev_midnight+c_s) {
            *time_to_add = unix_time_to_prev_midnight+c_d;
            // now is sunset
            *what_cal = 2;
        } else {
            *time_to_add = unix_time_to_prev_midnight+c_s;
            // now is sunrise
            *what_cal = 1;
        }
    } else if (now_time > unix_time_to_prev_midnight+c_s) {
        if (c_d > 0) {
            *time_to_add = unix_time_to_prev_midnight+c_d;
        } else {
            *time_to_add = unix_time_to_prev_midnight+c_r+(24*60*60); // tomorrow
        }
        // now is sunset
        *what_cal = 2;
    } else {
        *time_to_add = unix_time_to_prev_midnight+c_s;
        // now is sunrise
        *what_cal = 1;
    }
    
    //time_t next_time = *time_to_add;
    //printf("Next time is %s",ctime(&next_time));
    return 0;
}


static int wlrun(struct config cfg) {
    
    struct context ctx = {
        .sun = { 0,0,0,0 },
        .condition = 3,
        .state = STATE_INITIAL,
        .config = cfg,
    };
    
    if (icfile == 1 || icfile == 2 || icfile == 3 || icfile == 4) {
        int ret = get_data_array();
        if (ret == -1) {
            printf("Cannot read the file data_array.\n");
            icfile = 0;
        }
    }
    
    wl_list_init(&ctx.outputs);

    if (setup_signals(&ctx) == -1) {
        return EXIT_FAILURE;
    }

    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to create display\n");
        return EXIT_FAILURE;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &ctx);
    wl_display_roundtrip(display);

    if (ctx.gamma_control_manager == NULL) {
        fprintf(stderr, "compositor doesn't support wlr-gamma-control-unstable-v1\n");
        return EXIT_FAILURE;
    }
    struct output *output;
    wl_list_for_each(output, &ctx.outputs, link) {
        setup_gamma_control(&ctx, output);
    }
    wl_display_roundtrip(display);

    int time_to_add = 0; // sunrise/sunset and unix_time_to_prev_midnight of today or of tomorrow
    
    if (ctx.config.manual_time) {
        // calculate the next time change, when this program starts up
        f_time_to_add(ctx.config.sunrise,ctx.config.sunset,ctx.config.dusk,&time_to_add,&what_cal);
        
        if (ctx.config.sunset && ctx.config.sunrise) {
            struct itimerspec timerspec = set_struct2();
            set_timer2(timerspec,time_to_add);
        }
    }
    
    int htemp = ctx.config.high_temp;
    int ltemp = ctx.config.low_temp;
    int dtemp = ctx.config.dusk_temp;
    int temp = 0;
    float now_bright;
    if (what_cal == 1) { // now in sunrise time, next sunset
        if ( icfile == 0 && htemp != 6500) {
            temp = htemp;
            now_bright = sunrisebright;
        } else if ( icfile == 1 || icfile == 2 || icfile == 3 || icfile == 4) {
            htemp = 6500;
            temp = htemp; // 6500 with -f option
        } else if (htemp == 6500) {
            temp = htemp;
            now_bright = sunrisebright;
        }
    } else if (what_cal == 2) { // now is sunset, next dusk or sunrise
        temp = ltemp;
        now_bright = sunsetbright;
    } else if (what_cal == 3) { // now is dusk, next sunrise
        temp = dtemp;
        now_bright = duskbright;
    } else {
        temp = ltemp;
        now_bright = sunsetbright;
    }
    
    if (temp>0 && (temp != 6500||icfile)) {
        set_temperature(&ctx.outputs, temp, ctx.config.gamma, now_bright);
        fprintf(stderr, "Temperature setted to: %d K\n", temp);
    }
    
    int htemp2 = ctx.config.high_temp;
    int ltemp2 = ctx.config.low_temp;
    int dtemp2 = ctx.config.dusk_temp;
    int temp_to_step;
    int step = 10;
    int temp_step = 0;
    int time_to_remove = ctx.config.duration;
    int time_to_remove_step = (int)(time_to_remove/step);
    time_to_add = 0;
    f_time_to_add(ctx.config.sunrise,ctx.config.sunset,ctx.config.dusk,&time_to_add,&what_cal);
    
    if (what_cal == 1) { // now in sunrise time, next sunset
        if ( icfile == 0 && htemp != 6500) {
            temp = htemp;
        } else if ( icfile == 1 || icfile == 2 || icfile == 3 || icfile == 4) {
            htemp = 6500;
            temp = htemp; // 6500 with -f option
        } else if (htemp == 6500) {
            temp = htemp;
        }
    } else if (what_cal == 2) { // now is sunset, next dusk or sunrise
        temp = ltemp;
    } else if (what_cal == 3) { // now is dusk, next sunrise
        temp = dtemp;
    } else {
        temp = ltemp;
    }
    
    if (what_cal == 1) {
        temp_to_step = (htemp2-ltemp2);
    } else if (what_cal == 2) {
        if (dtemp > 0) {
            temp_to_step = (ltemp2-dtemp2);
        } else {
            temp_to_step = (ltemp2-htemp2);
        }
    } else if (what_cal == 3) {
        temp_to_step = (dtemp2-htemp2);
    }
    temp_step = (int)(temp_to_step/step);
    time_to_remove = ctx.config.duration;
    time_to_remove_step = (int)(time_to_remove/step);
    
    while (display_dispatch(display, -1) != -1) {
        if (ctx.new_output) {
            ctx.new_output = false;
            //timer_fired = true; // maybe needed
        }

        if (timer_fired) {
            timer_fired = false;
            
            if (time_to_remove == 0) {
                fprintf(stderr, "Temperature setted to: %d K\n", temp);
                // reset
                time_to_add = 0; // sunrise/sunset and unix_time_to_prev_midnight of today or of tomorrow
                what_cal = 0; // 0 reset
                f_time_to_add(ctx.config.sunrise,ctx.config.sunset,ctx.config.dusk,&time_to_add,&what_cal);
                struct itimerspec timerspec = set_struct2();
                set_timer2(timerspec,time_to_add);
                /*  next time data calculations  */
                if (what_cal == 1) {
                    temp_to_step = (htemp2-ltemp2);
                } else if (what_cal == 2) {
                    if (dtemp > 0) {
                        temp_to_step = (ltemp2-dtemp2);
                    } else {
                        temp_to_step = (ltemp2-htemp2);
                    }
                } else if (what_cal == 3) {
                    temp_to_step = (dtemp2-htemp2);
                }
                time_to_remove = ctx.config.duration;
                temp_step = (int)(temp_to_step/step);
            } else {
                struct itimerspec timerspec = set_struct2();
                time_to_add += time_to_remove_step;
                set_timer2(timerspec,time_to_add);
                temp -= temp_step;
                if (what_cal == 1) {
                    now_bright = sunrisebright;
                } else if (what_cal == 2) {
                    now_bright = sunsetbright;
                } else if (what_cal == 3) {
                    now_bright = duskbright;
                }
                set_temperature(&ctx.outputs, temp, ctx.config.gamma, now_bright);
                time_to_remove -= time_to_remove_step;
            }
        }
    }
    return EXIT_SUCCESS;
}

static const char usage[] = "usage: %s [options]\n"
"  -h               show this help message\n"
"  -v               show the version number\n"
"  -o <output>      name of output (display) to use,\n"
"                   by default all outputs are used\n"
"                   can be specified multiple times\n"
"  -f <type>        use the data_array file as starting curve; type: 1 or 2\n"
"  -T <temp>        set high temperature (default: 6500)\n"
"  -t <temp>        set low temperature (default: 4500)\n"
"  -m <temp>        set dusk temperature - optional (default: 0 - not used)\n"
"  -S <sunrise>     set manual sunrise (default 08:00)\n"
"  -s <sunset>      set manual sunset (default 18:00)\n"
"  -M <dusk>        set manual dusk - optional (e.g. 23:30)\n"
"  -d <duration>    set manual duration in seconds (default 60)\n"
"  -g <gamma>       set gamma (default: 1.0); not with the -f option\n"
"  -b <brightness>  set the brightness globally: 0.3-1.0\n"
"                   do not use with the -f option, use -B instead\n"
"  -B <b:b:b:>      set the brightness for each period of the day: 0.3-1.0\n"
"                   if used with the -f option, make sure to use 1.0:b:b\n";

int main(int argc, char *argv[]) {

    struct config config = {
        .high_temp = 6500,
        .low_temp = 4500,
        .dusk_temp = 4000,
        .gamma = 1.0,
        .duration = 60,
        .sunrise = 28800,
        .sunset = 64800,
    };
    str_vec_init(&config.output_names);
    
    
    int ret = EXIT_FAILURE;
    int opt;
    int duration_time;
    int temp_sunset_time;
    int temp_dusk_time;
    int aret = 0;
    while ((opt = getopt(argc, argv, "hvo:t:m:T:l:L:S:M:s:d:g:f:b:B:")) != -1) {
        switch (opt) {
            case 'o': // output
                str_vec_push(&config.output_names, optarg);
                break;
            case 'f':
                icfile = strtol(optarg, NULL, 10);
                break;
            case 'T': // sunrise temp
                config.high_temp = strtol(optarg, NULL, 10);
                if (icfile == 1 || icfile == 2 || icfile == 3 || icfile == 4) {
                    config.high_temp = 6500;
                }
                break;
            case 't': // sunrise temp
                config.low_temp = strtol(optarg, NULL, 10);
                break;
            case 'm': // dusk temp
                config.dusk_temp = strtol(optarg, NULL, 10);
                break;
            case 'S': // sunrise time
                if (parse_time_of_day(optarg, &config.sunrise) != 0) {
                    fprintf(stderr, "invalid time, expected HH:MM, got %s\n", optarg);
                    goto end;
                }
                config.manual_time = true;
                break;
            case 's': // sunset time
                temp_sunset_time = parse_time_of_day(optarg, &config.sunset);
                if (temp_sunset_time != 0) {
                    fprintf(stderr, "invalid time, expected HH:MM, got %s\n", optarg);
                    goto end;
                }
                if (config.sunset < config.sunrise) { // next day
                    config.sunset += (24*60*60);
                }
                config.manual_time = true;
                break;
            case 'M': // dusk time
                temp_dusk_time = parse_time_of_day(optarg, &config.dusk);
                if ( temp_dusk_time != 0) {
                    fprintf(stderr, "invalid time, expected HH:MM, got %s\n", optarg);
                    goto end;
                }
                if (config.dusk < config.sunset) { // next day
                    config.dusk += (24*60*60);
                }
                config.manual_time = true;
                break;
            case 'd':
                duration_time = strtol(optarg, NULL, 10);
                config.duration = (duration_time > 60) ? duration_time : 60;
                break;
            case 'g':
                config.gamma = strtod(optarg, NULL);
                break;
            case 'b':
                rgbbright = strtod(optarg, NULL);
                sunrisebright = rgbbright;
                sunsetbright = rgbbright;
                duskbright = rgbbright;
                break;
            case 'B':
                aret = calculate_bright(optarg, &sunrisebright, &sunsetbright, &duskbright);
                if (aret != 3) {
                    fprintf(stderr, "-B option: wrong values.\n");
                    goto end;
                }
                break;
            case 'v':
                printf("wlmonitorset version %s\n", WLMONITORSET_VERSION);
                ret = EXIT_SUCCESS;
                goto end;
            case 'h':
                ret = EXIT_SUCCESS;
            default:
                fprintf(stderr, usage, argv[0]);
                goto end;
        }
    }
    // forced
    config.manual_time = true;
    
    if (icfile == 1 || icfile == 2 || icfile == 3 || icfile == 4) {
        config.high_temp = 6500;
    }
    //if (config.high_temp <= config.low_temp) {
        //fprintf(stderr, "Low temp (%d) must be lower than high temp\n", config.low_temp);
        //goto end;
    //}
    //if (config.high_temp <= config.dusk_temp) {
        //fprintf(stderr, "Dusk temp (%d) must be lower than high temp\n", config.dusk_temp);
        //goto end;
    //}
    if (config.high_temp < 1500 || config.low_temp < 1500 || config.dusk_temp < 1500) {
        fprintf(stderr, "Temp (%d) must be higher than or equal to 1500\n", config.low_temp);
        goto end;
    }
    if (config.high_temp > 9000 || config.low_temp > 9000 || config.dusk_temp > 9000) {
        fprintf(stderr, "Temp (%d) must be lower than or equal to 9000\n", config.low_temp);
        goto end;
    }
    if (config.sunset && (config.sunrise+config.duration) >= config.sunset) {
        fprintf(stderr, "Sunrise time and/or duration wrong values: less than sunset.\n");
        goto end;
    }
    if (config.dusk && (config.sunset+config.duration) >= config.dusk) {
        fprintf(stderr, "Sunset time and/or duration wrong values: less than dusk.\n");
        goto end;
    }
    if (rgbbright < 0.3 || rgbbright > 1.0) {
        fprintf(stderr,"Brightness value out of range: %f instead of 0.3-1.0\n", rgbbright);
        goto end;
    }
    if (sunrisebright < 0.3 || sunrisebright > 1.0) {
        fprintf(stderr,"Sunrise brightness value out of range: %f instead of 0.3-1.0\n", sunrisebright);
        goto end;
    }
    if (sunsetbright < 0.3 || sunsetbright > 1.0) {
        fprintf(stderr,"Sunset brightness value out of range: %f instead of 0.3-1.0\n", sunsetbright);
        goto end;
    }
    if (duskbright < 0.3 || duskbright > 1.0) {
        fprintf(stderr,"Dusk brightness value out of range: %f instead of 0.3-1.0\n", duskbright);
        goto end;
    }
    ret = wlrun(config);
end:
    str_vec_free(&config.output_names);
    return ret;
}
