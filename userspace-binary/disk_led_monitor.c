#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>

#define DISKSTATS_PATH "/proc/diskstats"
#define MAX_LINE_LENGTH 1024
#define MONITOR_INTERVAL 100000  // 100ms in microseconds
#define LED_BLINK_DURATION 50000 // 50ms in microseconds

static volatile int running = 1;
static char *target_disk = NULL;
static char *target_led = NULL;
static int debug_mode = 0;

// Signal handler to gracefully exit
void signal_handler(int sig) {
    if (debug_mode) {
        printf("\nReceived signal %d, shutting down...\n", sig);
    }
    running = 0;
}

// Calculate hash of diskstats content to detect changes
unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash;
}

// Read specific disk line from diskstats and return hash
unsigned long get_diskstats_hash() {
    FILE *file = fopen(DISKSTATS_PATH, "r");
    if (!file) {
        if (debug_mode) {
            perror("Failed to open /proc/diskstats");
        }
        return 0;
    }
    
    char buffer[MAX_LINE_LENGTH];
    unsigned long hash = 0;
    int found = 0;
    
    while (fgets(buffer, sizeof(buffer), file)) {
        // Check if this line contains our target disk
        if (strstr(buffer, target_disk) != NULL) {
            hash = hash_string(buffer);
            found = 1;
            break;
        }
    }
    
    fclose(file);
    
    if (!found && debug_mode) {
        fprintf(stderr, "Warning: Disk '%s' not found in /proc/diskstats\n", target_disk);
    }
    
    return hash;
}

// Set LED to a specific brightness
int set_led_brightness(int brightness) {
    char led_brightness_path[256];
    snprintf(led_brightness_path, sizeof(led_brightness_path), 
             "/sys/class/leds/%s/brightness", target_led);
    
    FILE *file = fopen(led_brightness_path, "w");
    if (!file) {
        if (debug_mode) {
            perror("Failed to open LED brightness file");
        }
        return -1;
    }
    
    fprintf(file, "%d\n", brightness);
    fclose(file);
    return 0;
}

// Blink LED once
void blink_led() {
    set_led_brightness(1);
    usleep(LED_BLINK_DURATION);
    set_led_brightness(0);
}

// Check if specified LED exists
int check_led_exists() {
    char led_brightness_path[256];
    snprintf(led_brightness_path, sizeof(led_brightness_path), 
             "/sys/class/leds/%s/brightness", target_led);
    
    FILE *file = fopen(led_brightness_path, "r");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

// Check if specified disk exists in diskstats
int check_disk_exists() {
    FILE *file = fopen(DISKSTATS_PATH, "r");
    if (!file) {
        return 0;
    }
    
    char buffer[MAX_LINE_LENGTH];
    int found = 0;
    
    while (fgets(buffer, sizeof(buffer), file)) {
        if (strstr(buffer, target_disk) != NULL) {
            found = 1;
            break;
        }
    }
    
    fclose(file);
    return found;
}

void print_usage(const char *program_name) {
    printf("Usage: %s -d <disk> -l <led> [-v]\n", program_name);
    printf("  -d <disk>    Disk to monitor (e.g., sda, nvme0n1)\n");
    printf("  -l <led>     LED to control (e.g., led0, input0::capslock)\n");
    printf("  -v           Enable verbose/debug output\n");
    printf("\nExamples:\n");
    printf("  %s -d sda -l led0\n", program_name);
    printf("  %s -d nvme0n1 -l input0::capslock -v\n", program_name);
}

int main(int argc, char *argv[]) {
    int opt;
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "d:l:vh")) != -1) {
        switch (opt) {
            case 'd':
                target_disk = optarg;
                break;
            case 'l':
                target_led = optarg;
                break;
            case 'v':
                debug_mode = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Check if both arguments are provided
    if (!target_disk || !target_led) {
        fprintf(stderr, "\nERROR:\nYou must specify both the disk to monitor and the path to the LED to control\nBrowse /sys/class/leds for available LEDs to control.\n\nNote that this program must be run with elevated privileges to change LED state!\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (debug_mode) {
        printf("Disk LED Monitor - Userspace Version\n");
        printf("Monitoring disk: %s\n", target_disk);
        printf("Controlling LED: %s\n", target_led);
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Check if LED exists
    if (!check_led_exists()) {
        fprintf(stderr, "ERROR: LED '%s' not found in /sys/class/leds/\n", target_led);
        return 1;
    }
    
    // Check if disk exists
    if (!check_disk_exists()) {
        fprintf(stderr, "ERROR: Disk '%s' not found in /proc/diskstats\n", target_disk);
        return 1;
    }
    
    // Check if we can access diskstats
    if (access(DISKSTATS_PATH, R_OK) != 0) {
        perror("Cannot read /proc/diskstats");
        return 1;
    }
    
    if (debug_mode) {
        printf("Starting disk activity monitoring...\n");
        printf("Press Ctrl+C to stop\n");
    }
    
    unsigned long last_hash = get_diskstats_hash();
    unsigned long current_hash;
    int activity_count = 0;
    
    while (running) {
        current_hash = get_diskstats_hash();
        
        if (current_hash != last_hash && current_hash != 0) {
            activity_count++;
            if (debug_mode) {
                printf("Disk activity detected on %s! (Count: %d)\n", target_disk, activity_count);
            }
            
            // Blink the LED
            blink_led();
            
            last_hash = current_hash;
        }
        
        usleep(MONITOR_INTERVAL);
    }
    
    if (debug_mode) {
        printf("Shutting down. Total disk activities detected: %d\n", activity_count);
    }
    return 0;
} 
