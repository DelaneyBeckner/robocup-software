#include <stdio.h>
#include <stdlib.h>

#include "control.h"
#include "fpga.h"
#include "main.h"
#include "console.h"
#include "power.h"
#include "status.h"
#include "stall.h"
#include "radio_protocol.h"
#include "encoder_monitor.h"
#include "stall.h"

const controller_info_t *default_controller;

////////

static void dumb_update()
{
	for (int i = 0; i < 5; ++i)
	{
		drive_mode[i] = DRIVE_SLOW_DECAY;
	}
	
	for (int i = 0; i < 4; ++i)
	{
		motor_out[i] = -wheel_command[i] * 511 / 127;
	}
	motor_out[4] = dribble_command >> 1;
}

////////

static int step_motor;
static int step_level = 0;

static void step_init(int argc, const char *argv[])
{
	if (argc == 2)
	{
		step_motor = parse_int(argv[0]);
		step_level = parse_int(argv[1]);
	}
	
	if (argc != 2 || step_motor < 0 || step_motor > 4 || step_level < -MOTOR_MAX || step_level > MOTOR_MAX)
	{
		printf("Usage: run step <motor 0..3> <level>\n");
		controller = 0;
	}
}

static void step_update()
{
	int supply_mv = supply_raw * VBATT_NUM / VBATT_DIV;
	printf("%2d.%03d %4d %3d", supply_mv / 1000, supply_mv % 1000, encoder_delta[step_motor], hall_delta[step_motor]);
	
	int failed = (motor_faults | encoder_faults | motor_stall) & (1 << step_motor);
	if (failed)
	{
		printf(" *");
	}
	printf("\n");
	
	if (failed && encoder_delta[step_motor] == 0 && hall_delta[step_motor] == 0)
	{
		// Motor died
		printf("Motor failed\n");
		controller = 0;
	}
	
	motor_out[step_motor] = step_level;
	drive_mode[step_motor] = DRIVE_SLOW_DECAY;
}

////////

#define Log_Count 100
static int log_pos;
static int log_level;
static struct
{
	int16_t delta[4];
} log_data[Log_Count];
static uint8_t log_started;

static void log_init(int argc, const char *argv[])
{
	log_pos = 0;
	log_started = 0;
	if (argc)
	{
		log_level = parse_uint32(argv[0]);
	} else {
		printf("Usage: run log <level> [start]\n");
		controller = 0;
	}
	
	if (argc > 1)
	{
		log_started = 1;
	}
}

static void log_update()
{
	if (log_pos == Log_Count)
	{
		log_started = 0;
	} else if (kick_strength)
	{
		log_started = 1;
	}
	
	if (log_started)
	{
		for (int i = 0; i < 4; ++i)
		{
			log_data[log_pos].delta[i] = encoder_delta[i];
		}
		++log_pos;
		
		motor_out[0] = -log_level;
		motor_out[1] = -log_level;
		motor_out[2] = log_level;
		motor_out[3] = log_level;
	}
}

static void log_print()
{
	controller = 0;
	printf("\n");
	for (int i = 0; i < Log_Count; ++i)
	{
		printf("%3d", i);
		for (int j = 0; j < 4; ++j)
		{
			printf(" %5d", log_data[i].delta[j]);
		}
		printf("\n");
	}
	printf("DONE\n");
}

////////

static const int Command_Rate_Limit = 160 * 256;
static int kp = 160;
static int kd = 160;
static int last_out[4];
static int last_error[4];
static int pd_debug = -1;

static void pd_init(int argc, const char *argv[])
{
	for (int i = 0; i < 4; ++i)
	{
		last_out[i] = 0;
		last_error[i] = 0;
	}
	
	// First two parameters are coefficients
	if (argc >= 2)
	{
		kp = parse_int(argv[0]);
		kd = parse_int(argv[1]);
	}
	
	// Third parameter is which motor to print
	if (argc >= 3)
	{
		pd_debug = parse_int(argv[2]);
	} else {
		pd_debug = -1;
	}
}

static void pd_update()
{
	if (base2008)
	{
		// Nope
		for (int i = 0; i < 5; ++i)
		{
			drive_mode[i] = DRIVE_OFF;
		}
		return;
	}
	
	for (int i = 0; i < 5; ++i)
	{
		drive_mode[i] = DRIVE_SLOW_DECAY;
	}
	
	for (int i = 0; i < 4; ++i)
	{
		int setpoint = -wheel_command[i] * 3;
		int speed = encoder_delta[i];
		int error = setpoint - speed;

		if (abs(error) <= 1)
		{
				error = 0;
		}

		int delta_error = error - last_error[i];
		last_error[i] = error;
		int delta = error * kp + delta_error * kd;
		
		// Limit the change between consecutive cycles to prevent excessive current
		if (delta > Command_Rate_Limit)
		{
			delta = Command_Rate_Limit;
		} else if (delta < -Command_Rate_Limit)
		{
			delta = -Command_Rate_Limit;
		}
		last_out[i] += delta;
		
		// Don't accumulate output for broken motors, in case they start working
		if ((motor_faults | motor_stall) & (1 << i))
		{
			last_out[i] = 0;
		}
		
		// Clip to output limits
		if (last_out[i] > MOTOR_MAX * 256)
		{
			last_out[i] = MOTOR_MAX * 256;
		} else if (last_out[i] < -MOTOR_MAX * 256)
		{
			last_out[i] = -MOTOR_MAX * 256;
		}
		
		if (i == pd_debug)
		{
			printf("%02x %08x %08x %08x\n", motor_faults, speed, delta, last_out[i]);
		}
		
		motor_out[i] = last_out[i] / 256;
	}

	//FIXME - Do we need speed control on the dribbler?
	motor_out[4] = -dribble_command;
}

////////

const controller_info_t controllers[] =
{
	{"dumb", 0, 0, dumb_update},
	{"pd", pd_init, 0, pd_update},
	{"step", step_init, 0, step_update},
	{"log", log_init, 0, log_update},
	{"log_print", 0, 0, log_print},
	
	// End of table
	{0, 0}
};

const controller_info_t *controller = 0;
