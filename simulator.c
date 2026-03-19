#include "simulator.h"
#include "global_vars.h"
#include "audio_hint.h"
#include "db_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAX_SIM_FILES 8
#define SAMPLE_INTERVAL_MS 100
#define STEP_THRESHOLD 1200.0f
#define STEP_RISE 80.0f
#define STEPS_PER_FLOOR 20

typedef struct {
    float gx;
    float gy;
    float gz;
    float ax;
    float ay;
    float az;
} SensorSample;

static char s_files[MAX_SIM_FILES][260];
static int s_file_count = 0;
static int s_file_index = 0;
static int s_last_line = 0;
static bool s_loop_mode = true;
static FILE* s_current_fp = NULL;
static char s_profile[32] = "mixed";

static FILE* open_with_fallback(const char* path) {
    FILE* fp = fopen(path, "r");
    if (fp) {
        return fp;
    }

    char candidate[320];
    snprintf(candidate, sizeof(candidate), "..\\%s", path);
    fp = fopen(candidate, "r");
    if (fp) {
        return fp;
    }

    snprintf(candidate, sizeof(candidate), "..\\..\\%s", path);
    fp = fopen(candidate, "r");
    if (fp) {
        return fp;
    }

    return NULL;
}

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static void close_current_file(void) {
    if (s_current_fp) {
        fclose(s_current_fp);
        s_current_fp = NULL;
    }
}

static bool open_current_file(void) {
    close_current_file();
    if (s_file_count <= 0 || s_file_index < 0 || s_file_index >= s_file_count) {
        return false;
    }

    s_current_fp = open_with_fallback(s_files[s_file_index]);
    if (!s_current_fp) {
        return false;
    }

    char header[256];
    if (!fgets(header, sizeof(header), s_current_fp)) {
        close_current_file();
        return false;
    }
    return true;
}

static bool open_next_file(void) {
    if (s_file_count <= 0) {
        return false;
    }

    int attempts = 0;
    while (attempts < s_file_count) {
        if (open_current_file()) {
            return true;
        }
        s_file_index = (s_file_index + 1) % s_file_count;
        attempts++;
    }
    return false;
}

static bool parse_sample_line(const char* line, SensorSample* sample) {
    return sscanf(line, "%f %f %f %f %f %f",
        &sample->gx, &sample->gy, &sample->gz,
        &sample->ax, &sample->ay, &sample->az) == 6;
}

static bool read_next_sample(SensorSample* sample) {
    if (s_file_count <= 0) {
        return false;
    }

    if (!s_current_fp) {
        if (!open_next_file()) {
            return false;
        }
    }

    char line[256];
    while (1) {
        if (fgets(line, sizeof(line), s_current_fp)) {
            if (parse_sample_line(line, sample)) {
                s_last_line++;
                return true;
            }
            continue;
        }

        close_current_file();
        s_file_index++;

        if (s_file_index >= s_file_count) {
            if (!s_loop_mode) {
                return false;
            }
            s_file_index = 0;
        }

        if (!open_next_file()) {
            return false;
        }
    }
}

void init_users(void) {
    for (int i = 0; i < MAX_USERS; ++i) {
        pthread_mutex_lock(&g_users[i].mutex);
        g_users[i].current_floor = 1;
        g_users[i].total_steps = 0;
        g_users[i].speed_per_minute = 0.0f;
        g_users[i].sent_lines = 0;
        g_users[i].buffer_index = 0;
        memset(g_users[i].accel_buffer, 0, sizeof(g_users[i].accel_buffer));
        memset(g_users[i].gyro_buffer, 0, sizeof(g_users[i].gyro_buffer));
        pthread_mutex_unlock(&g_users[i].mutex);
    }
}

void sim_set_data_files(const char** files, int count, bool loop_mode) {
    s_file_count = 0;
    s_file_index = 0;
    s_last_line = 0;
    s_loop_mode = loop_mode;

    if (files && count > 0) {
        int limit = count > MAX_SIM_FILES ? MAX_SIM_FILES : count;
        for (int i = 0; i < limit; ++i) {
            snprintf(s_files[i], sizeof(s_files[i]), "%s", files[i]);
            s_file_count++;
        }
    }

    close_current_file();
}

bool sim_load_profile(const char* profile) {
    if (!profile) {
        return false;
    }

    if (strcmp(profile, "mixed") == 0) {
        const char* files[] = {"data1.txt", "data2.txt"};
        sim_set_data_files(files, 2, true);
        snprintf(s_profile, sizeof(s_profile), "mixed");
        return true;
    }

    if (strcmp(profile, "upstairs3") == 0) {
        const char* files[] = {"data1.txt"};
        sim_set_data_files(files, 1, true);
        snprintf(s_profile, sizeof(s_profile), "upstairs3");
        return true;
    }

    return false;
}

const char* sim_get_profile(void) {
    return s_profile;
}

void sim_start(void) {
    if (g_system_state.start_time == 0) {
        g_system_state.start_time = time(NULL);
    }
    g_system_state.simulation_running = true;
}

void sim_stop(void) {
    g_system_state.simulation_running = false;
}

void sim_reset(void) {
    init_users();
    g_total_climbed = 0;
    g_current_floor = 1;
    g_speed_per_minute = 0.0f;
    g_system_state.start_time = time(NULL);
    s_file_index = 0;
    s_last_line = 0;
    close_current_file();
}

void reset_simulation(void) {
    sim_reset();
}

bool sim_is_running(void) {
    return g_system_state.simulation_running;
}

int sim_get_current_floor(int user_id) {
    if (user_id < 0 || user_id >= MAX_USERS) {
        return 1;
    }

    int floor;
    pthread_mutex_lock(&g_users[user_id].mutex);
    floor = g_users[user_id].current_floor;
    pthread_mutex_unlock(&g_users[user_id].mutex);
    return floor;
}

int sim_get_total_climbed(int user_id) {
    if (user_id < 0 || user_id >= MAX_USERS) {
        return 0;
    }

    int steps;
    pthread_mutex_lock(&g_users[user_id].mutex);
    steps = g_users[user_id].total_steps;
    pthread_mutex_unlock(&g_users[user_id].mutex);
    return steps;
}

double sim_get_speed(int user_id) {
    if (user_id < 0 || user_id >= MAX_USERS) {
        return 0.0;
    }

    double speed;
    pthread_mutex_lock(&g_users[user_id].mutex);
    speed = g_users[user_id].speed_per_minute;
    pthread_mutex_unlock(&g_users[user_id].mutex);
    return speed;
}

int sim_get_sent_lines(int user_id) {
    if (user_id < 0 || user_id >= MAX_USERS) {
        return s_last_line;
    }

    int sent;
    pthread_mutex_lock(&g_users[user_id].mutex);
    sent = g_users[user_id].sent_lines;
    pthread_mutex_unlock(&g_users[user_id].mutex);
    return sent;
}

void* simulator_thread_func(void* arg) {
    (void)arg;

    SensorSample sample = {0};
    float last_filtered = 0.0f;
    int cooldown = 0;

    while (g_system_running) {
        if (g_reset_requested || g_system_state.reset_requested) {
            sim_reset();
            g_reset_requested = false;
            g_system_state.reset_requested = false;
            last_filtered = 0.0f;
            cooldown = 0;
        }

        if (!sim_is_running()) {
            sleep_ms(100);
            continue;
        }

        if (!read_next_sample(&sample)) {
            sim_stop();
            sleep_ms(100);
            continue;
        }

        float accel_magnitude = sqrtf(sample.ax * sample.ax + sample.ay * sample.ay + sample.az * sample.az);
        float gyro_magnitude = sqrtf(sample.gx * sample.gx + sample.gy * sample.gy + sample.gz * sample.gz);

        User* user = &g_users[0];
        pthread_mutex_lock(&user->mutex);

        user->accel_buffer[user->buffer_index] = accel_magnitude;
        user->gyro_buffer[user->buffer_index] = gyro_magnitude;
        user->buffer_index = (user->buffer_index + 1) % DATA_BUFFER_SIZE;
        user->sent_lines = s_last_line;

        float filtered = (last_filtered * 0.8f) + (accel_magnitude * 0.2f);

        bool is_step = false;
        if (cooldown > 0) {
            cooldown--;
        } else if ((filtered > STEP_THRESHOLD) && ((filtered - last_filtered) > STEP_RISE)) {
            is_step = true;
            cooldown = 2;
        }

        if (is_step) {
            user->total_steps++;
            int floor = 1 + (user->total_steps / STEPS_PER_FLOOR);
            if (floor > g_max_floors) {
                floor = g_max_floors;
            }
            user->current_floor = floor;

            time_t now = time(NULL);
            double elapsed = difftime(now, g_system_state.start_time);
            if (elapsed > 0.0) {
                user->speed_per_minute = (float)(user->total_steps / (elapsed / 60.0));
            }

            g_current_floor = user->current_floor;
            g_total_climbed = user->current_floor - 1;
            g_speed_per_minute = user->speed_per_minute;

            save_progress(user->user_id, user->current_floor, user->total_steps, user->speed_per_minute);
            check_audio_hint(user->current_floor, user->total_steps);
        }

        last_filtered = filtered;
        pthread_mutex_unlock(&user->mutex);

        sleep_ms(SAMPLE_INTERVAL_MS);
    }

    close_current_file();
    return NULL;
}
