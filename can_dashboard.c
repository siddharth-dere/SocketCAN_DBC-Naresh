/*
 * can_dashboard.c
 * Version B - SocketCAN receiver with generic Intel bit-field extraction.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <net/if.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define CAN_INTERFACE "vcan0"
#define PAYLOAD_BYTES 8U

static volatile sig_atomic_t running = 1;

typedef struct {
    double vehicle_speed;
    double engine_rpm;
    double coolant_temperature;
    double fuel_level;
    double battery_voltage;
    double ambient_temperature;
    unsigned status_seen;
    unsigned thermal_seen;
    unsigned power_seen;
} dashboard_state_t;

static void request_shutdown(int signo)
{
    (void)signo;
    running = 0;
}

static int bind_can_interface(const char *interface_name)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }

    struct ifreq request;
    memset(&request, 0, sizeof(request));
    if (snprintf(request.ifr_name, IFNAMSIZ, "%s", interface_name) >= IFNAMSIZ) {
        fprintf(stderr, "Interface name too long: %s\n", interface_name);
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

static uint32_t get_intel_bits(const uint8_t payload[PAYLOAD_BYTES],
                               unsigned start_bit, unsigned bit_count)
{
    uint32_t result = 0U;

    for (unsigned i = 0; i < bit_count; ++i) {
        unsigned absolute_bit = start_bit + i;
        unsigned byte_index = absolute_bit / 8U;
        unsigned bit_index = absolute_bit % 8U;
        unsigned bit = (payload[byte_index] >> bit_index) & 1U;
        result |= (uint32_t)bit << i;
    }

    return result;
}

static double physical_value(uint32_t raw, double factor, double offset)
{
    return (double)raw * factor + offset;
}

static void consume_vehicle_status(const struct can_frame *frame,
                                   dashboard_state_t *state)
{
    uint32_t speed_raw = get_intel_bits(frame->data, 0U, 16U);
    uint32_t rpm_raw = get_intel_bits(frame->data, 16U, 16U);

    state->vehicle_speed = physical_value(speed_raw, 0.01, 0.0);
    state->engine_rpm = physical_value(rpm_raw, 1.0, 0.0);
    state->status_seen++;
}

static void consume_thermal_fuel(const struct can_frame *frame,
                                 dashboard_state_t *state)
{
    uint32_t coolant_raw = get_intel_bits(frame->data, 32U, 11U);
    uint32_t fuel_raw = get_intel_bits(frame->data, 48U, 8U);

    state->coolant_temperature = physical_value(coolant_raw, 0.1, -40.0);
    state->fuel_level = physical_value(fuel_raw, 0.5, 0.0);
    state->thermal_seen++;
}

static void consume_power_status(const struct can_frame *frame,
                                 dashboard_state_t *state)
{
    uint32_t battery_raw = get_intel_bits(frame->data, 0U, 12U);
    uint32_t ambient_raw = get_intel_bits(frame->data, 24U, 8U);

    state->battery_voltage = physical_value(battery_raw, 0.01, 0.0);
    state->ambient_temperature = physical_value(ambient_raw, 1.0, -40.0);
    state->power_seen++;
}

static void process_can_frame(const struct can_frame *frame,
                              dashboard_state_t *state)
{
    canid_t identifier = frame->can_id & CAN_SFF_MASK;

    if (identifier == 0x100U && frame->can_dlc >= 4U) {
        consume_vehicle_status(frame, state);
    } else if (identifier == 0x101U && frame->can_dlc >= 7U) {
        consume_thermal_fuel(frame, state);
    } else if (identifier == 0x102U && frame->can_dlc >= 4U) {
        consume_power_status(frame, state);
    }
}

static void render(const dashboard_state_t *state)
{
    printf("\033[H\033[2J");
    puts("+-----------------------------------------------------------+");
    puts("|             SOCKETCAN VEHICLE TELEMETRY V2               |");
    puts("+-----------------------------------------------------------+");
    printf("| Interface : %-44s |\n", CAN_INTERFACE);
    puts("+----------------------+----------------------+-------------+");
    puts("| Message              | Signal               | Value       |");
    puts("+----------------------+----------------------+-------------+");
    printf("| 0x100 VehicleStatus  | VehicleSpeed         | %7.2f km/h |\n",
           state->vehicle_speed);
    printf("| 0x100 VehicleStatus  | EngineRPM            | %7.0f rpm  |\n",
           state->engine_rpm);
    printf("| 0x101 ThermalFuel    | CoolantTemperature   | %7.1f C    |\n",
           state->coolant_temperature);
    printf("| 0x101 ThermalFuel    | FuelLevel            | %7.1f %%    |\n",
           state->fuel_level);
    printf("| 0x102 PowerStatus    | BatteryVoltage       | %7.2f V    |\n",
           state->battery_voltage);
    printf("| 0x102 PowerStatus    | AmbientTemperature   | %7.1f C    |\n",
           state->ambient_temperature);
    puts("+----------------------+----------------------+-------------+");
    printf("| Frames observed: 100=%-6u 101=%-6u 102=%-6u            |\n",
           state->status_seen, state->thermal_seen, state->power_seen);
    puts("| Press Ctrl+C to terminate.                                  |");
    puts("+-----------------------------------------------------------+");
    fflush(stdout);
}

int main(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_shutdown;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    int fd = bind_can_interface(CAN_INTERFACE);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    dashboard_state_t state;
    memset(&state, 0, sizeof(state));

    puts("Waiting for CAN frames on vcan0...");

    while (running) {
        struct can_frame frame;
        ssize_t received = recv(fd, &frame, sizeof(frame), 0);

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv(CAN frame)");
            close(fd);
            return EXIT_FAILURE;
        }

        if (received != (ssize_t)sizeof(frame)) {
            fprintf(stderr, "Unexpected frame length: %zd bytes\n", received);
            continue;
        }

        if ((frame.can_id & CAN_EFF_FLAG) != 0U) {
            continue;
        }

        process_can_frame(&frame, &state);
        render(&state);
    }

    close(fd);
    puts("Dashboard stopped.");
    return EXIT_SUCCESS;
}
