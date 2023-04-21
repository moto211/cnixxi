// Could be defined here, or in the processor defines.
#define SYSTEM_CORE_CLOCK 48000000

#include "ch32v003fun.h"
#include <stdio.h>

#define APB_CLOCK SYSTEM_CORE_CLOCK

uint32_t count;

#define ABSOLUTE_MAX_ADC_SET 368 //172 volts (definitely do not exceed)
#define PWM_PERIOD 70
#define PWM_MAXIMUM_DUTY 40
#define ERROR_P_TERM 0 // Actually a shift.  0 is rattl-y but averages out and gives tight control.

int target_feedback = 0;
int lastadc = 0;

void ADC1_IRQHandler(void) __attribute__((interrupt));
void ADC1_IRQHandler(void)
{
	int adc = lastadc = ADC1->RDATAR;
	int err = target_feedback - adc;
	ADC1->STATR &= ~ADC_EOC;

	if( err < 0 )
		TIM1->CH2CVR = 0;
	else
	{
		err = err >> ERROR_P_TERM;
		if( err > PWM_MAXIMUM_DUTY ) err = PWM_MAXIMUM_DUTY;
		TIM1->CH2CVR = err;
	}

	count++;
}

static void SetupTimer()
{
	// Main inductor is ~5uH.
	// Our peak current is ~200mA
	// Our target cycle duty is ~1/6
	// Our nominal voltage is ~4V
	// 4V / .000005H = 800000A/s / 0.2 = 0.00000025 = 250nS, but we are only on for 1/6 of the time., or 1.5uS.  Let's set our period to be 64/48 = 652nS.

	// GPIO A1 Push-Pull, Auto Function, 50 MHz Drive Current
	GPIOA->CFGLR &= ~(0xf<<(4*1));
	GPIOA->CFGLR |= (GPIO_Speed_50MHz | GPIO_CNF_OUT_PP_AF)<<(4*1);

	// Enable Timer 1
	RCC->APB2PRSTR |= RCC_APB2Periph_TIM1;
	RCC->APB2PRSTR &= ~RCC_APB2Periph_TIM1;

	TIM1->PSC = 0x0000;  // Prescalar to 0x0000 (so, 24MHz base clock)
	TIM1->ATRLR = PWM_PERIOD;
	TIM1->SWEVGR = TIM_UG;
	TIM1->CCER = TIM_CC2E | TIM_CC2NP;  // CH2 is control for FET.
	TIM1->CHCTLR1 = TIM_OC2M_2 | TIM_OC2M_1;

	TIM1->CH2CVR = 0;  // Actual duty cycle.

	// Setup TRGO for ADC.  TODO: this should be on update (TIM_MMS_1)
	TIM1->CTLR2 = TIM_MMS_1;

	// Enable TIM1 outputs
	TIM1->BDTR = TIM_MOE;
	TIM1->CTLR1 = TIM_CEN;
}

static void SetupADC()
{
	// Configure ADC.
	// PD4 is analog input chl 7
	GPIOD->CFGLR &= ~(0xf<<(4*4));	// CNF = 00: Analog, MODE = 00: Input
	
	// Reset the ADC to init all regs
	RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
	RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;
	
	// Set up single conversion on chl 7
	ADC1->RSQR1 = 0;
	ADC1->RSQR2 = 0;
	ADC1->RSQR3 = 7;	// 0-9 for 8 ext inputs and two internals
	
	// set sampling time for chl 7
	ADC1->SAMPTR2 = 6<<(3*7);	// 0:7 => 3/9/15/30/43/57/73/241 cycles
		// (4 == 43 cycles), (6 = 73 cycles)  Note these are alrady /2, so 
		// setting this to 73 cycles actually makes it wait 256 total cycles
		// @ 48MHz.

	// turn on ADC and set rule group to sw trig
	ADC1->CTLR2 |= ADC_ADON; // 0 = Use TRGO event for Timer 1.
	
	// Reset calibration
	ADC1->CTLR2 |= ADC_RSTCAL;
	while(ADC1->CTLR2 & ADC_RSTCAL);
	
	// Calibrate
	ADC1->CTLR2 |= ADC_CAL;
	while(ADC1->CTLR2 & ADC_CAL);

	// Allow Timer1 TRGO to trigger ADC conversion.
	ADC1->CTLR2 |= ADC_EXTTRIG;

	// enable the ADC Conversion Complete IRQ
	NVIC_EnableIRQ( ADC_IRQn );

	// Enable the End-of-conversion interrupt.
	ADC1->CTLR1 = ADC_EOCIE;
}

int main()
{
	SystemInit48HSI();
	SetupDebugPrintf();
	Delay_Ms( 10 );

	// Enable Peripherals
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOA
				   | RCC_APB2Periph_TIM1 | RCC_APB2Periph_ADC1;

	GPIOD->CFGLR = 
		(GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*6) | // GPIO D6 Push-Pull  (for debug)
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*7) | // DIG_AUX
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*3) | // DIG_9
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*2) | // DIG_8
		(GPIO_Speed_10MHz | GPIO_CNF_IN_FLOATING)<<(4*1) | // Leave PGM pin floating, dont make it an ADC.
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*0);  // DIG_DOT

	GPIOC->CFGLR = 
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*0) | // DIG_0
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*1) | // DIG_1
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*2) | // DIG_2
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*3) | // DIG_3
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*4) | // DIG_4
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*5) | // DIG_5
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*6) | // DIG_6
		(GPIO_Speed_50MHz | GPIO_CNF_OUT_PP)<<(4*7);  // DIG_7

		

	GPIOC->BSHR = 1<<4;

	SetupADC();
	SetupTimer();

	*DMDATA0 = 0;

	while(1)
	{
		GPIOD->BSHR = 1<<6;
		GPIOD->BSHR = (1<<(16+6));
		uint32_t dmdword = *DMDATA0;
		if( (dmdword & 0xf0) == 0x40 )
		{
			// ./minichlink -s 0x04 0x01110040
			// ./minichlink -g 0x04

			// It is a valid status word back from the PC.
			int feedback = dmdword>>20;
			if( feedback > ABSOLUTE_MAX_ADC_SET ) feedback = ABSOLUTE_MAX_ADC_SET;
			target_feedback = feedback;

			int segmenton = (dmdword>>16)&0x0f;
			// Other various things are lower.

			// Turn everything off.
			GPIOC->BSHR = 0xff<<16;
			GPIOD->BSHR = 0b10001101 << 16;

			if( segmenton >= 1 )
			{
				segmenton--;
				if( segmenton < 8 )
					GPIOC->BSHR = 1<<segmenton;
				else if( segmenton == 8 )
					GPIOD->BSHR = 1<<2;
				else if( segmenton == 9 )
					GPIOD->BSHR = 1<<3;
				else if( segmenton == 10 )
					GPIOD->BSHR = 1<<0;
				else if( segmenton == 11 )
					GPIOD->BSHR = 1<<7;
			}

			*DMDATA0 = lastadc << 16;
		}
	}
}
