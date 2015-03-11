
`include "phase_driver.vh"

module PWM_LOW(clock, duty_cycle, pwm);

	input clock;
	input [`DUTY_CYCLE_WIDTH - 1:0] duty_cycle;
	output pwm;
	reg pwm = 0;

reg [`COUNTER_WIDTH - 1:0] counter = `DEAD_TIME;
always @ (posedge clock)
begin
 	counter = counter + 1;
	if (counter >= `MAX_COUNTER) counter = 0;
	pwm = 	(duty_cycle == 0) ? 1 :
			(counter >= (2*`DEAD_TIME + duty_cycle)*`DUTY_CYCLE_STEP_RES) ? 1 : 0;
end
endmodule