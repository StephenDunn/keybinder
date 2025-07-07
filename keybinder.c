#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

// Debounce time in milliseconds (adjust as needed)
#define DEBOUNCE_TIME_MS 90

// Maximum number of keys to track
#define MAX_KEYS 256

// Structure to track key timing
struct key_state {
    struct timespec last_press;
    int is_pressed;
};

// Get current time in milliseconds
long long get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Calculate time difference in milliseconds
long long time_diff_ms(struct timespec *start, struct timespec *end) {
    long long start_ms = (long long)start->tv_sec * 1000 + start->tv_nsec / 1000000;
    long long end_ms = (long long)end->tv_sec * 1000 + end->tv_nsec / 1000000;
    return end_ms - start_ms;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        fprintf(stderr, "Find your keyboard with: cat /proc/bus/input/devices | grep -A 4 -B 4 keyboard\n");
        return 1;
    }

    const char *device_path = argv[1];
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        fprintf(stderr, "Make sure to run as root or add your user to the input group\n");
        return 1;
    }

    // Initialize libevdev
    struct libevdev *dev;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to init libevdev (%s)\n", strerror(-rc));
        close(fd);
        return 1;
    }

    // Grab the device to prevent double input
    rc = libevdev_grab(dev, LIBEVDEV_GRAB);
    if (rc < 0) {
        fprintf(stderr, "Failed to grab device (%s)\n", strerror(-rc));
        libevdev_free(dev);
        close(fd);
        return 1;
    }

    // Create uinput device for filtered output
    struct libevdev_uinput *uidev;
    rc = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
    if (rc != 0) {
        fprintf(stderr, "Failed to create uinput device (%s)\n", strerror(-rc));
        libevdev_free(dev);
        close(fd);
        return 1;
    }

    // Initialize key state tracking
    struct key_state key_states[MAX_KEYS] = {0};
    
    // Track startup time to ignore initial events
    struct timespec startup_time;
    clock_gettime(CLOCK_MONOTONIC, &startup_time);
    int startup_grace_ms = 1000; // 1 second grace period

    printf("Keyboard debounce filter started for %s\n", libevdev_get_name(dev));
    printf("Debounce time: %d ms\n", DEBOUNCE_TIME_MS);
    printf("Starting with 1 second grace period...\n");
    printf("Press Ctrl+C to stop\n");

    // Main event loop
    while (1) {
        struct input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            // Handle key events
            if (ev.type == EV_KEY && ev.code < MAX_KEYS) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                
                // Check if we're still in startup grace period
                long long time_since_startup = time_diff_ms(&startup_time, &now);
                int in_grace_period = (time_since_startup < startup_grace_ms);
                
                // Check if this is a key press (value 1) or release (value 0)
                if (ev.value == 1) { // Key press
                    // During grace period, don't filter anything - just pass through
                    if (in_grace_period) {
                        key_states[ev.code].last_press = now;
                        key_states[ev.code].is_pressed = 1;
                    } else {
                        // Normal filtering after grace period
                        long long time_since_last = 0;
                        if (key_states[ev.code].last_press.tv_sec != 0) {
                            time_since_last = time_diff_ms(&key_states[ev.code].last_press, &now);
                        }
                        
                        // Only filter if this is a very recent repeat of the same key
                        if (time_since_last > 0 && time_since_last < DEBOUNCE_TIME_MS && 
                            !key_states[ev.code].is_pressed) {
                            printf("Filtered double press: key %d (time: %lld ms)\n", ev.code, time_since_last);
                            continue; // Skip this event
                        }
                        
                        // Update key state
                        key_states[ev.code].last_press = now;
                        key_states[ev.code].is_pressed = 1;
                    }
                    
                } else if (ev.value == 0) { // Key release
                    key_states[ev.code].is_pressed = 0;
                } else if (ev.value == 2) { // Key repeat/hold
                    // For key repeats, just pass them through without filtering
                    // These are legitimate auto-repeat events
                }
            }
            
            // Forward the event to the virtual device
            libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
        } else if (rc == -EAGAIN) {
            // No events available right now
            usleep(1000); // Sleep for 1ms
        } else {
            fprintf(stderr, "Error reading event: %s\n", strerror(-rc));
            break;
        }
    }

    // Cleanup
    libevdev_grab(dev, LIBEVDEV_UNGRAB);
    libevdev_uinput_destroy(uidev);
    libevdev_free(dev);
    close(fd);
    
    return 0;
}
