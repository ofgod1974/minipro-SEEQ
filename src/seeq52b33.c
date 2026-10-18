/*
 * SEEQ 52B33, 8192 x 8, DIP28, custom T48 algorithm.
 * GPL-3.0-or-later; see LICENSE.
 * Datasheet: SEEQ Data Book 1988, pp. 1-11..1-16 (October 1987).
 * Experimental: software tested, electrical operation NOT hardware validated.
 * /CC is powered HIGH; never use Chip Clear or VPP. Each byte is explicitly
 * erased with FF before programming. /WE stays LOW for >=12 ms.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "database.h"
#include "bitbang.h"
#include "t48.h"
#include "seeq52b33.h"
#ifdef SEEQ52B33_TEST
int seeq_test_nanosleep(const struct timespec *, struct timespec *);
#define nanosleep seeq_test_nanosleep
#endif

#define SEEQ_SIZE 8192u
#define PULSE_US 12000u
/* T48 bitbang uses the virtual 40-pin frame, not physical ZIF48 numbers.
 * For DIP28: left column 1..14, right column 27..40. */
#define SLOT(p) ((p) <= 14 ? (p)-1 : (p)+11)
#define CC SLOT(1)
#define CE SLOT(20)
#define OE SLOT(22)
#define WE SLOT(27)
static uint8_t addr_pins[] = {10,9,8,7,6,5,4,3,25,24,21,23,2,0};
static uint8_t data_pins[] = {11,12,13,15,16,17,18,19,0};
static uint8_t control_pins[] = {20,22,27,0};
static uint8_t dir[T48_BB_PIN_COUNT], state[T48_BB_PIN_COUNT];
static int active, data_output;
static volatile sig_atomic_t cancelled;
static void (*old_int)(int), (*old_term)(int);
static int signals_installed;

static void cancel(int sig) { (void)sig; cancelled = 1; }

/* Interrupted sleeps must not shorten a programming pulse, even on Ctrl-C.
 * USB READ_PINS below acts as a device-side execution barrier before sleep. */
static int wait_us(unsigned long us)
{
	struct timespec req = {us / 1000000, (us % 1000000) * 1000};
	while (nanosleep(&req, &req)) {
		if (errno != EINTR) return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int fail(minipro_handle_t *h)
{
	/* Best effort only: a failed/disconnected USB cannot guarantee cleanup. */
	state[WE] = 1;
	t48_set_zif_state(h, state);
	state[CE] = state[OE] = 1;
	t48_set_zif_state(h, state);
	t48_reset_state(h);
	active = 0;
	return EXIT_FAILURE;
}

static int idle(minipro_handle_t *h)
{
	/* Raise /WE before changing any other latched input. */
	state[WE] = 1;
	if (t48_set_zif_state(h, state)) return EXIT_FAILURE;
	state[CE] = state[OE] = 1;
	return t48_set_zif_state(h, state);
}

int seeq52b33_validate(minipro_handle_t *h)
{
	device_t *d = h->device;
	cmdopts_t *o = h->cmdopts;
	if (h->version != MP_T48 || !d->flags.custom_protocol ||
	    d->protocol_id != CP_SEEQ52B33 || d->variant != 0 ||
	    d->package_details.pin_count != 28 || d->code_memory_size != SEEQ_SIZE ||
	    d->pulse_delay != PULSE_US || d->voltages.vcc != 0 ||
	    d->voltages.vdd != 0 || d->voltages.vpp != 0 ||
	    (o && (o->icsp || o->pincheck || o->blank_skip || o->protect_on ||
	           o->protect_off || o->action == LOGIC_IC_TEST))) {
		fprintf(stderr, "SEEQ: requires T48, dedicated DIP28 profile, fixed 5 V/12 ms; "
		        "no ICSP, pin test, blank-skip or protection options.\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int seeq52b33_init(minipro_handle_t *h)
{
	pin_driver_t power[T48_BB_PIN_COUNT] = {{0}};
	if (seeq52b33_validate(h)) return EXIT_FAILURE;
	if (t48_reset_state(h)) return EXIT_FAILURE;
	cancelled = 0;
	old_int = signal(SIGINT, cancel);
	old_term = signal(SIGTERM, cancel);
	signals_installed = 1;
	memset(dir, MP_PIN_DIRECTION_IN | MP_PIN_PULLUP, sizeof(dir));
	memset(state, 0, sizeof(state));
	set_io_pins(dir, addr_pins, MP_PIN_DIRECTION_OUT, 28);
	set_io_pins(dir, control_pins, MP_PIN_DIRECTION_OUT, 28);
	state[CC] = state[CE] = state[OE] = state[WE] = 1;
	data_output = 0;
	/* Set inactive controls while socket power is disconnected. All input
	 * pullups keep /WE high during the global direction reset. */
	if (t48_set_zif_direction(h, dir) || t48_set_zif_state(h, state))
		return fail(h);
	power[SLOT(14)].gnd = 1;
	power[SLOT(28)].vcc = power[CC].vcc = 1;
	if (t48_seeq_power_on(h, power)) return fail(h);
	active = 1;
	fprintf(stderr, "SEEQ 52B33 experimental: 5 V, /CC HIGH, byte erase/write >=12 ms.\n");
	return EXIT_SUCCESS;
}

int seeq52b33_terminate(minipro_handle_t *h)
{
	int ret = 0;
	if (active) ret = idle(h);
	ret |= t48_reset_state(h);
	active = 0;
	if (signals_installed) {
		if (old_int != SIG_ERR && old_int != cancel) signal(SIGINT, old_int);
		if (old_term != SIG_ERR && old_term != cancel) signal(SIGTERM, old_term);
		signals_installed = 0;
	}
	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int data_mode(minipro_handle_t *h, int output)
{
	if (output == data_output) return EXIT_SUCCESS;
	/* idle() must be completed BEFORE resetting any directions. */
	if (idle(h)) return EXIT_FAILURE;
	set_io_pins(dir, data_pins, output ? MP_PIN_DIRECTION_OUT :
	            MP_PIN_DIRECTION_IN | MP_PIN_PULLUP, 28);
	if (t48_set_zif_direction(h, dir) || t48_set_zif_state(h, state))
		return EXIT_FAILURE;
	data_output = output;
	return EXIT_SUCCESS;
}

static int read_byte(minipro_handle_t *h, uint32_t address, uint8_t *value)
{
	uint8_t pins[T48_BB_PIN_COUNT];
	if (idle(h) || data_mode(h, 0)) return EXIT_FAILURE;
	set_bits(state, addr_pins, address, 28);
	if (t48_set_zif_state(h, state)) return EXIT_FAILURE;
	state[CE] = state[OE] = 0;
	if (t48_set_zif_state(h, state) || wait_us(5) ||
	    t48_get_zif_state(h, pins)) return EXIT_FAILURE;
	*value = get_bits(pins, data_pins, 28);
	return idle(h);
}

static int pulse(minipro_handle_t *h, uint32_t address, uint8_t value)
{
	uint8_t pins[T48_BB_PIN_COUNT];
	if (cancelled || idle(h) || data_mode(h, 1)) return EXIT_FAILURE;
	set_bits(state, addr_pins, address, 28);
	set_bits(state, data_pins, value, 28);
	if (t48_set_zif_state(h, state)) return EXIT_FAILURE;
	state[CE] = 0; /* /OE remains HIGH */
	if (t48_set_zif_state(h, state) || wait_us(5)) return EXIT_FAILURE;
	state[WE] = 0;
	if (t48_set_zif_state(h, state) || t48_get_zif_state(h, pins) ||
	    pins[CC] != 1 || pins[CE] != 0 || pins[OE] != 1 || pins[WE] != 0 ||
	    get_bits(pins, addr_pins, 28) != address ||
	    get_bits(pins, data_pins, 28) != value ||
	    wait_us(PULSE_US)) return EXIT_FAILURE;
	state[WE] = 1;
	if (t48_set_zif_state(h, state) || t48_get_zif_state(h, pins) ||
	    wait_us(5) || idle(h)) return EXIT_FAILURE;
	return cancelled ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int write_byte(minipro_handle_t *h, uint32_t a, uint8_t value)
{
	uint8_t old;
	if (read_byte(h, a, &old)) return EXIT_FAILURE;
	if (old == value) return EXIT_SUCCESS;
	if (old != 0xff) {
		if (pulse(h, a, 0xff) || read_byte(h, a, &old)) return EXIT_FAILURE;
		if (old != 0xff) goto mismatch;
	}
	if (value != 0xff && pulse(h, a, value)) return EXIT_FAILURE;
	if (read_byte(h, a, &old)) return EXIT_FAILURE;
	if (old == value) return EXIT_SUCCESS;
mismatch:
	fprintf(stderr, "SEEQ: erase/write verification failed at 0x%04X "
	        "(target %02X, read %02X).\n", (unsigned)a, value, old);
	return EXIT_FAILURE;
}

static int valid_range(uint32_t address, size_t len)
{
	return address < SEEQ_SIZE && len > 0 && len <= SEEQ_SIZE - address;
}

int seeq52b33_read(minipro_handle_t *h, uint32_t a, uint8_t *buf, size_t len)
{
	if (!active || !buf || !valid_range(a, len)) return EXIT_FAILURE;
	for (size_t i = 0; i < len; i++)
		if (cancelled || read_byte(h, a+i, buf+i)) return fail(h);
	return EXIT_SUCCESS;
}

int seeq52b33_write(minipro_handle_t *h, uint32_t a, const uint8_t *buf, size_t len)
{
	if (!active || !buf || !valid_range(a, len)) return EXIT_FAILURE;
	for (size_t i = 0; i < len; i++)
		if (cancelled || write_byte(h, a+i, buf[i])) return fail(h);
	return EXIT_SUCCESS;
}

int seeq52b33_erase(minipro_handle_t *h)
{
	if (!active) return EXIT_FAILURE;
	for (uint32_t a = 0; a < SEEQ_SIZE; a++) {
		if (cancelled || write_byte(h, a, 0xff)) return fail(h);
		if ((a & 255) == 255) fprintf(stderr, "\rSEEQ byte erase: %u/8192", (unsigned)a+1);
	}
	fprintf(stderr, "\n");
	return EXIT_SUCCESS;
}
