/*
 * can_transmitter.c
 * Version B - SocketCAN telemetry publisher using generic Intel bit-field packing.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <math.h>
#include <net/if.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define CAN_INTERFACE "vcan0"
#define FRAME_LENGTH 8U
#define PERIOD_MS 100L

static volatile sig_atomic_t running = 1;

static void handle_stop(int signo)
{
    (void)signo;
    running = 0;
}

static int connect_socketcan(const char *name)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }

    struct ifreq request;
    memset(&request, 0, sizeof(request));
    if (snprintf(request.ifr_name, IFNAMSIZ, "%s", name) >= IFNAMSIZ) {
        fprintf(stderr, "Interface name too long: %s\n", name);
        close(fd);
        return -1;
    }

    if (ioctl(fd, SIOCGIFINDEX, &request) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(fd);
        return -1;
    }

    struct sockaddr_can endpoint;
    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.can_family = AF_CAN;
    endpoint.can_ifindex = request.ifr_ifindex;

    if (bind(fd, (struct sockaddr *)&endpoint, sizeof(endpoint)) < 0) {
        perror("bind(AF_CAN)");
        close(fd);
        return -1;
    }

    return fd;
}

static uint32_t encode_physical(double physical, double factor, double offset,
                                uint32_t raw_min, uint32_t raw_max)
{
    double raw = (physical - offset) / factor;

    if (raw <= (double)raw_min) {
        return raw_min;
    }
    if (raw >= (double)raw_max) {
        return raw_max;
    }
    return (uint32_t)llround(raw);
}

static void set_intel_bits(uint8_t payload[FRAME_LENGTH], unsigned start_bit,
                           unsigned bit_count, uint32_t value)
{
    for (unsigned i = 0; i < bit_count; ++i) {
        unsigned absolute_bit = start_bit + i;
        unsigned byte_index = absolute_bit / 8U;
        unsigned bit_index = absolute_bit % 8U;
        uint8_t mask = (uint8_t)(1U << bit_index);

        if ((value >> i) & 1U) {
            payload[byte_index] |= mask;
        } else {
            payload[byte_index] &= (uint8_t)~mask;
        }
    }
}

static int publish_frame(int fd, canid_t id, const uint8_t payload[FRAME_LENGTH])
{
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = id;
    frame.can_dlc = FRAME_LENGTH;
    memcpy(frame.data, payload, FRAME_LENGTH);

    ssize_t count = send(fd, &frame, sizeof(frame), 0);
    if (count != (ssize_t)sizeof(frame)) {
        if (count < 0) {
            fprintf(stderr, "CAN send 0x%03X failed: %s\n", id, strerror(errno));
        } else {
            fprintf(stderr, "CAN send 0x%03X was incomplete.\n", id);
        }
        return -1;
    }
    return 0;
}

static void frame_vehicle_status(uint8_t data[FRAME_LENGTH], double speed, double rpm)
{
    memset(data, 0, FRAME_LENGTH);
    uint32_t speed_raw = encode_physical(speed, 0.01, 0.0, 0U, 12000U);
    uint32_t rpm_raw = encode_physical(rpm, 1.0, 0.0, 800U, 5000U);

    set_intel_bits(data, 0U, 16U, speed_raw);
    set_intel_bits(data, 16U, 16U, rpm_raw);
}

static void frame_thermal_fuel(uint8_t data[FRAME_LENGTH], double coolant, double fuel)
{
    memset(data, 0, FRAME_LENGTH);
    uint32_t coolant_raw = encode_physical(coolant, 0.1, -40.0, 0U, 1600U);
    uint32_t fuel_raw = encode_physical(fuel, 0.5, 0.0, 0U, 200U);

    set_intel_bits(data, 32U, 11U, coolant_raw);
    set_intel_bits(data, 48U, 8U, fuel_raw);
}

static void frame_power_status(uint8_t data[FRAME_LENGTH], double battery, double ambient)
{
    memset(data, 0, FRAME_LENGTH);
    uint32_t battery_raw = encode_physical(battery, 0.01, 0.0, 1100U, 1500U);
    uint32_t ambient_raw = encode_physical(ambient, 1.0, -40.0, 0U, 140U);

    set_intel_bits(data, 0U, 12U, battery_raw);
    set_intel_bits(data, 24U, 8U, ambient_raw);
}

static double monotonic_seconds(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void delay_ms(long milliseconds)
{
    struct timespec remaining;
    remaining.tv_sec = milliseconds / 1000L;
    remaining.tv_nsec = (milliseconds % 1000L) * 1000000L;

    while (nanosleep(&remaining, &remaining) < 0 && errno == EINTR && running) {
    }
}

static void print_sample(double speed, double rpm, double coolant,
                         double fuel, double battery, double ambient)
{
    printf("TX  Speed=%6.2f km/h | RPM=%5.0f | Coolant=%6.1f C | Fuel=%5.1f %% | Battery=%4.2f V | Ambient=%5.1f C\n",
           speed, rpm, coolant, fuel, battery, ambient);
    fflush(stdout);
}

int main(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    int fd = connect_socketcan(CAN_INTERFACE);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    puts("---------------------------------------------------------------");
    puts("SocketCAN telemetry publisher - Version B");
    printf("Interface: %s | update period: %ld ms\n", CAN_INTERFACE, PERIOD_MS);
    puts("---------------------------------------------------------------");

    const double t0 = monotonic_seconds();

    while (running) {
        double t = monotonic_seconds() - t0;

        double speed = 58.0 + 52.0 * sin(0.21 * t);
        double rpm = 2600.0 + 1350.0 * sin(0.17 * t + 0.65);
        double coolant = 72.0 + 20.0 * sin(0.06 * t + 0.25);
        double fuel = 82.0 - 0.03 * t;
        double battery = 13.1 + 0.55 * sin(0.29 * t + 1.2);
        double ambient = 30.0 + 9.0 * sin(0.035 * t);

        if (fuel < 0.0) {
            fuel = 0.0;
        }

        uint8_t vehicle[FRAME_LENGTH];
        uint8_t thermal[FRAME_LENGTH];
        uint8_t power[FRAME_LENGTH];

        frame_vehicle_status(vehicle, speed, rpm);
        frame_thermal_fuel(thermal, coolant, fuel);
        frame_power_status(power, battery, ambient);

        if (publish_frame(fd, 0x100U, vehicle) != 0 ||
            publish_frame(fd, 0x101U, thermal) != 0 ||
            publish_frame(fd, 0x102U, power) != 0) {
            close(fd);
            return EXIT_FAILURE;
        }

        print_sample(speed, rpm, coolant, fuel, battery, ambient);
        delay_ms(PERIOD_MS);
    }

    close(fd);
    puts("Transmitter stopped.");
    return EXIT_SUCCESS;
}
