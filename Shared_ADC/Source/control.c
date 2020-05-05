#include <MKL25Z4.H>
#include <stdio.h>
#include <stdint.h>
#include <cmsis_os2.h>

#include "gpio_defs.h"
#include "debug.h"

#include "control.h"
#include "LCD_driver.h"

#include "timers.h"
#include "delay.h"
#include "LEDs.h"
#include "UI.h"

#include "FX.h"

volatile int32_t g_duty_cycle=5;  // global to give debugger access

volatile int g_enable_flash=1;
volatile int g_peak_set_current=FLASH_CURRENT_MA; // Peak flash current
volatile int g_flash_duration=FLASH_DURATION_MS;
volatile int g_flash_period=FLASH_PERIOD_MS; 

volatile int time1,time2, time_took;

volatile int g_enable_control=1;
volatile int g_set_current=0; // Default starting LED current

int g_measured_accum[NUMBER_SAMPLES_NEEDED];
int g_set_accum[NUMBER_SAMPLES_NEEDED];
int samples_taken = 0;
int state = 0;
volatile int direction;

volatile int g_measured_current;
volatile int error;

volatile uint16_t time_remaining, diff;

int32_t pGain_8 = PGAIN_8; // proportional gain numerator scaled by 2^8

typedef struct {
	uint32_t chan_num;
	uint32_t *pointer_result;
} READ_TS_2_ADC;

typedef struct{
	uint32_t chan_num;		//  channel number
	uint32_t result;			//	value of result
} ADC_2_READ_TS;

READ_TS_2_ADC queued_in;
ADC_2_READ_TS queued_out;

extern osMessageQueueId_t q_read_TS, q_post_ADC;

SPid plantPID = {0, // dState
	0, // iState
	LIM_DUTY_CYCLE, // iMax
	-LIM_DUTY_CYCLE, // iMin
	P_GAIN_FL, // pGain
	I_GAIN_FL, // iGain
	D_GAIN_FL  // dGain
};

SPidFX plantPID_FX = {FL_TO_FX(0), // dState
	FL_TO_FX(0), // iState
	FL_TO_FX(LIM_DUTY_CYCLE), // iMax
	FL_TO_FX(-LIM_DUTY_CYCLE), // iMin
	P_GAIN_FX, // pGain
	I_GAIN_FX, // iGain
	D_GAIN_FX  // dGain
};

float UpdatePID(SPid * pid, float error, float position){
	float pTerm, dTerm, iTerm;

	// calculate the proportional term
	pTerm = pid->pGain * error;
	// calculate the integral state with appropriate limiting
	pid->iState += error;
	if (pid->iState > pid->iMax) 
		pid->iState = pid->iMax;
	else if (pid->iState < pid->iMin) 
		pid->iState = pid->iMin;
	iTerm = pid->iGain * pid->iState; // calculate the integral term
	dTerm = pid->dGain * (position - pid->dState);
	pid->dState = position;

	return pTerm + iTerm - dTerm;
}

FX16_16 UpdatePID_FX(SPidFX * pid, FX16_16 error_FX, FX16_16 position_FX){
	FX16_16 pTerm, dTerm, iTerm, diff, ret_val;

	// calculate the proportional term
	pTerm = Multiply_FX(pid->pGain, error_FX);

	// calculate the integral state with appropriate limiting
	pid->iState = Add_FX(pid->iState, error_FX);
	if (pid->iState > pid->iMax) 
		pid->iState = pid->iMax;
	else if (pid->iState < pid->iMin) 
		pid->iState = pid->iMin;
	
	iTerm = Multiply_FX(pid->iGain, pid->iState); // calculate the integral term
	diff = Subtract_FX(position_FX, pid->dState);
	dTerm = Multiply_FX(pid->dGain, diff);
	pid->dState = position_FX;

	ret_val = Add_FX(pTerm, iTerm);
	ret_val = Subtract_FX(ret_val, dTerm);
	return ret_val;
}

void Control_HBLED(void) {
	uint16_t res;
	FX16_16 change_FX, error_FX;
	FPTB->PSOR = MASK(DBG_CONTROLLER);
	
#if USE_ADC_INTERRUPT
	// already completed conversion, so don't wait
#else
	while (!(ADC0->SC1[0] & ADC_SC1_COCO_MASK))
		; // wait until end of conversion
#endif
	res = ADC0->R[0];

	g_measured_current = (res*1500)>>16; // Extra Credit: Make this code work: V_REF_MV*MA_SCALING_FACTOR)/(ADC_FULL_SCALE*R_SENSE)

	if (g_enable_control) {
		switch (control_mode) {
			case OpenLoop:
					// don't do anything!
				break;
			case BangBang:
				if (g_measured_current < g_set_current)
					g_duty_cycle = LIM_DUTY_CYCLE;
				else
					g_duty_cycle = 0;
				break;
			case Incremental:
				if (g_measured_current < g_set_current)
					g_duty_cycle += INC_STEP;
				else
					g_duty_cycle -= INC_STEP;
				break;
			case Proportional:
				g_duty_cycle += (pGain_8*(g_set_current - g_measured_current))/256; //  - 1;
			break;
			case PID:
				g_duty_cycle += UpdatePID(&plantPID, g_set_current - g_measured_current, g_measured_current);
				break;
			case PID_FX:
				error_FX = INT_TO_FX(g_set_current - g_measured_current);
				change_FX = UpdatePID_FX(&plantPID_FX, error_FX, INT_TO_FX(g_measured_current));
				g_duty_cycle += FX_TO_INT(change_FX);
			break;
			default:
				break;
		}
	
		// Update PWM controller with duty cycle
		if (g_duty_cycle < 0)
			g_duty_cycle = 0;
		else if (g_duty_cycle > LIM_DUTY_CYCLE)
			g_duty_cycle = LIM_DUTY_CYCLE;
		PWM_Set_Value(TPM0, PWM_HBLED_CHANNEL, g_duty_cycle);
	} // if g_enable_control
	
	// Samples current and setpoint values of the HBLED to write to display once values have accumulated
	
	switch(state){
		case INITALIZE:
			for(int samples = 0; samples < 100; samples++){
				g_measured_accum[samples] = 0;
				g_set_accum[samples] = 0;
				samples_taken = SAMPLE_OFFSET;
			}
			state = WAITING;
			break;
		
		case WAITING:
			if(g_set_current != 0)
				state = SAMPLING;
			break;
		
		case SAMPLING:
			if(samples_taken < NUMBER_SAMPLES_NEEDED-1){
			samples_taken++;
			g_measured_accum[samples_taken] = g_measured_current;
			g_set_accum[samples_taken] = g_set_current;
			}
			else
				state = WAITING;
			break;
		
	}

	FPTB->PCOR = MASK(DBG_CONTROLLER);
}

#if USE_ADC_INTERRUPT
void ADC0_IRQHandler() {
	osStatus_t result;
	static int prev_conv_type = PRIORITY;
	FPTB->PSOR = MASK(DBG_IRQ_ADC);
	switch(prev_conv_type) {
		case PRIORITY:
			Control_HBLED();
		break;
		case REQ_FROM_QUEUE:
			queued_out.chan_num = queued_in.chan_num;
			queued_out.result = ADC0->R[0];
			result = osMessageQueuePut(q_post_ADC, &queued_out, NULL, 0);
	}
	time_remaining = check_timing();
	
	if((osMessageQueueGetCount(q_read_TS) != 0) /*&& (time_remaining > MAX_TIME_NEEDED)*/) { 
		result = osMessageQueueGet(q_read_TS, &queued_in, NULL, 0);
		if(result == osOK){
			switch(queued_in.chan_num){
				case LCD_TS_YU_CHANNEL:
					LCD_TS_YU_PORT->PCR[LCD_TS_YU_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_YU_PORT->PCR[LCD_TS_YU_BIT] |= PORT_PCR_MUX(0);
					LCD_TS_YD_PORT->PCR[LCD_TS_YD_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_YD_PORT->PCR[LCD_TS_YD_BIT] |= PORT_PCR_MUX(0);
							
					// Configure outputs to GPIO
					LCD_TS_XL_PORT->PCR[LCD_TS_XL_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_XL_PORT->PCR[LCD_TS_XL_BIT] |= PORT_PCR_MUX(1);
					LCD_TS_XR_PORT->PCR[LCD_TS_XR_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_XR_PORT->PCR[LCD_TS_XR_BIT] |= PORT_PCR_MUX(1);
					LCD_TS_XL_PT->PDDR |= MASK(LCD_TS_XL_BIT); 
					LCD_TS_XR_PT->PDDR |= MASK(LCD_TS_XR_BIT);
					LCD_TS_XR_PT->PSOR = MASK(LCD_TS_XR_BIT); // Set XR to 1
					LCD_TS_XL_PT->PCOR = MASK(LCD_TS_XL_BIT); // Clear XL to 0
					// Wait for inputs to settle
					osDelay(TS_DELAY);
					break;
				case LCD_TS_XL_CHANNEL:
					LCD_TS_XL_PORT->PCR[LCD_TS_XL_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_XL_PORT->PCR[LCD_TS_XL_BIT] |= PORT_PCR_MUX(0);
					LCD_TS_XR_PORT->PCR[LCD_TS_XR_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_XR_PORT->PCR[LCD_TS_XR_BIT] |= PORT_PCR_MUX(0);
					// Disable pull-up - just to be sure
					LCD_TS_XR_PORT->PCR[LCD_TS_XR_BIT] &= ~PORT_PCR_PE_MASK; 
							
					// Configure outputs to GPIO
					LCD_TS_YU_PORT->PCR[LCD_TS_YU_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_YU_PORT->PCR[LCD_TS_YU_BIT] |= PORT_PCR_MUX(1);
					LCD_TS_YD_PORT->PCR[LCD_TS_YD_BIT] &= ~PORT_PCR_MUX_MASK;
					LCD_TS_YD_PORT->PCR[LCD_TS_YD_BIT] |= PORT_PCR_MUX(1);
					LCD_TS_YU_PT->PDDR |= MASK(LCD_TS_YU_BIT);
					LCD_TS_YD_PT->PDDR |= MASK(LCD_TS_YD_BIT);
					LCD_TS_YD_PT->PSOR = MASK(LCD_TS_YD_BIT); // Set YD to 1
					LCD_TS_YU_PT->PCOR = MASK(LCD_TS_YU_BIT); // Clear YU to 0
					// Wait for the inputs to settle
					osDelay(TS_DELAY);
				break;
			}
			ADC0->SC2 |= ADC_SC2_ADTRG(0);
			ADC0->SC1[0] &= ~ADC_SC1_ADCH_MASK;
 			ADC0->SC1[0] |= ADC_SC1_ADCH(queued_in.chan_num);
			ADC0->SC1[0] = ADC_SC1_AIEN(1) | queued_in.chan_num;
			prev_conv_type = REQ_FROM_QUEUE;
		}
		else{
		#if USE_ADC_FOR_BUCK
			// Configure ADC to read Ch 8 (FPTB 0)
			SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK; 
			ADC0->CFG1 = 0x0C; // 16 bit
			//	ADC0->CFG2 = ADC_CFG2_ADLSTS(3);
			ADC0->SC2 = ADC_SC2_REFSEL(0);

		#if USE_ADC_HW_TRIGGER
			// Enable hardware triggering of ADC
			ADC0->SC2 |= ADC_SC2_ADTRG(1);
			// Select triggering by TPM0 Overflow
			SIM->SOPT7 = SIM_SOPT7_ADC0TRGSEL(8) | SIM_SOPT7_ADC0ALTTRGEN_MASK;
			// Select input channel 
			ADC0->SC1[0] &= ~ADC_SC1_ADCH_MASK;
			ADC0->SC1[0] |= ADC_SC1_ADCH(ADC_SENSE_CHANNEL);
		#endif // USE_ADC_HW_TRIGGER

		#if USE_ADC_INTERRUPT 
			// enable ADC interrupt
			ADC0->SC1[0] |= ADC_SC1_AIEN(1);

			// Configure NVIC for ADC interrupt
			NVIC_SetPriority(ADC0_IRQn, 128); // 0, 64, 128 or 192
			NVIC_ClearPendingIRQ(ADC0_IRQn); 
			NVIC_EnableIRQ(ADC0_IRQn);	
		#endif // USE_ADC_INTERRUPT
		#endif // USE_ADC_FOR_BUCK
		prev_conv_type = PRIORITY;
		}
	}
	else {
		#if USE_ADC_FOR_BUCK
			// Configure ADC to read Ch 8 (FPTB 0)
			SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK; 
			ADC0->CFG1 = 0x0C; // 16 bit
			//	ADC0->CFG2 = ADC_CFG2_ADLSTS(3);
			ADC0->SC2 = ADC_SC2_REFSEL(0);

		#if USE_ADC_HW_TRIGGER
			// Enable hardware triggering of ADC
			ADC0->SC2 |= ADC_SC2_ADTRG(1);
			// Select triggering by TPM0 Overflow
			SIM->SOPT7 = SIM_SOPT7_ADC0TRGSEL(8) | SIM_SOPT7_ADC0ALTTRGEN_MASK;
			// Select input channel 
			ADC0->SC1[0] &= ~ADC_SC1_ADCH_MASK;
			ADC0->SC1[0] |= ADC_SC1_ADCH(ADC_SENSE_CHANNEL);
		#endif // USE_ADC_HW_TRIGGER

		#if USE_ADC_INTERRUPT 
			// enable ADC interrupt
			ADC0->SC1[0] |= ADC_SC1_AIEN(1);

			// Configure NVIC for ADC interrupt
			NVIC_SetPriority(ADC0_IRQn, 128); // 0, 64, 128 or 192
			NVIC_ClearPendingIRQ(ADC0_IRQn); 
			NVIC_EnableIRQ(ADC0_IRQn);	
		#endif // USE_ADC_INTERRUPT
		#endif // USE_ADC_FOR_BUCK
		prev_conv_type = PRIORITY;
	}
	FPTB->PCOR = MASK(DBG_IRQ_ADC);
}
#endif

uint16_t check_timing(void){
	uint16_t x,y;
	// uint16_t direction;
	
	
	x = TPM0->CNT;
	y = TPM0->CNT;
	diff = x-y;
	direction = diff>0 ? DOWN : UP;
	
	if(direction == DOWN){
		return TPM0->CNT;  // Must count down then up to MOD
	}
	else {		// UP
		return (TPM0->MOD - TPM0->CNT);	 // Must only go up to MOD
	}
}
void Set_DAC(unsigned int code) {
	// Force 16-bit write to DAC
	uint16_t * dac0dat = (uint16_t *)&(DAC0->DAT[0].DATL);
	*dac0dat = (uint16_t) code;
}

void Set_DAC_mA(unsigned int current) {
	unsigned int code = MA_TO_DAC_CODE(current);
	// Force 16-bit write to DAC
	uint16_t * dac0dat = (uint16_t *)&(DAC0->DAT[0].DATL);
	*dac0dat = (uint16_t) code;
}

void Init_DAC_HBLED(void) {
  // Enable clock to DAC and Port E
	SIM->SCGC6 |= SIM_SCGC6_DAC0_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;
	
	// Select analog for pin
	PORTE->PCR[DAC_POS] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[DAC_POS] |= PORT_PCR_MUX(0);	
		
	// Disable buffer mode
	DAC0->C1 = 0;
	DAC0->C2 = 0;
	
	// Enable DAC, select VDDA as reference voltage
	DAC0->C0 = DAC_C0_DACEN_MASK | DAC_C0_DACRFS_MASK;
	Set_DAC(0);
}

void Init_ADC_HBLED(void) {
#if USE_ADC_FOR_BUCK
	// Configure ADC to read Ch 8 (FPTB 0)
	SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK; 
	ADC0->CFG1 = 0x0C; // 16 bit
	//	ADC0->CFG2 = ADC_CFG2_ADLSTS(3);
	ADC0->SC2 = ADC_SC2_REFSEL(0);

#if USE_ADC_HW_TRIGGER
	// Enable hardware triggering of ADC
	ADC0->SC2 |= ADC_SC2_ADTRG(1);
	// Select triggering by TPM0 Overflow
	SIM->SOPT7 = SIM_SOPT7_ADC0TRGSEL(8) | SIM_SOPT7_ADC0ALTTRGEN_MASK;
	// Select input channel 
	ADC0->SC1[0] &= ~ADC_SC1_ADCH_MASK;
	ADC0->SC1[0] |= ADC_SC1_ADCH(ADC_SENSE_CHANNEL);
#endif // USE_ADC_HW_TRIGGER

#if USE_ADC_INTERRUPT 
	// enable ADC interrupt
	ADC0->SC1[0] |= ADC_SC1_AIEN(1);

	// Configure NVIC for ADC interrupt
	NVIC_SetPriority(ADC0_IRQn, 128); // 0, 64, 128 or 192
	NVIC_ClearPendingIRQ(ADC0_IRQn); 
	NVIC_EnableIRQ(ADC0_IRQn);	
#endif // USE_ADC_INTERRUPT
#endif // USE_ADC_FOR_BUCK
}

void Update_Set_Current(void) {
	static int delay=0;

	if (delay == 0)					// Just for initialization from global
		delay = g_flash_period;
	
	if (g_enable_flash){
		delay--;
		if (delay == g_flash_duration) { // assumes runs every 1 ms
			g_set_current = g_peak_set_current;
			Set_DAC_mA(g_set_current);
		} else  if (delay == 0) {
			delay = g_flash_period;
			g_set_current = 0;
			Set_DAC_mA(g_set_current);
		}
	}
}

void Init_Buck_HBLED(void) {
	Init_DAC_HBLED();
	Init_ADC_HBLED();
	
	// Configure driver for buck converter
	// Set up PTE31 to use for SMPS with TPM0 Ch 4
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;
	PORTE->PCR[31]  &= PORT_PCR_MUX(7);
	PORTE->PCR[31]  |= PORT_PCR_MUX(3);
	PWM_Init(TPM0, PWM_HBLED_CHANNEL, PWM_PERIOD, g_duty_cycle, 0, 0);
	
}

// Handler functions (callbacks)
// Default handlers
void Control_OnOff_Handler (UI_FIELD_T * fld, int v) {
	if (fld->Val != NULL) {
		if (v > 0) {
			*fld->Val = 1;
		} else {
			*fld->Val = 0;
		}
	}
}

void Control_IntNonNegative_Handler (UI_FIELD_T * fld, int v) {
	int n;
	if (fld->Val != NULL) {
		n = *fld->Val + v/16;
		if (n < 0) {
			n = 0;
		}
		*fld->Val = n;
	}
}

void Control_DutyCycle_Handler(UI_FIELD_T * fld, int v) {
	int dc;
	if (fld->Val != NULL) {
		dc = g_duty_cycle + v/16;
		if (dc < 0)
			dc = 0;
		else if (dc > LIM_DUTY_CYCLE)
			dc = LIM_DUTY_CYCLE;
		*(fld->Val) = dc;
		PWM_Set_Value(TPM0, PWM_HBLED_CHANNEL, g_duty_cycle);
	}
}
