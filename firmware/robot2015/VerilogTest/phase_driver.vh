`ifndef _phase_driver_vh_
`define _phase_driver_vh_

`define DEAD_TIME 1 //dead time for pwm_low in clock ticks

`define COUNTER_WIDTH 8 //bits available to counter
`define MAX_COUNTER 8'hF0 //PWM period = MAX_COUNTER * clockPeriod 

`define DUTY_CYCLE_WIDTH 8 //bits available to duty_cycle
`define DUTY_CYCLE_STEP_RES 1 //ceil(MAX_COUNTER/2^(duty_cycle bits))

`endif //_phase_driver_vh_