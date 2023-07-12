/*Program writen by Maxim Dorokhin https://github.com/m0sBuS
	This program is example for simple BLDC-motor controller with IR2104 and STP75NF75
	STM32F103C8T6 "BluePill" is mainboard for this project
	This program use two independed timers (Advanced-control timer TIM1 as 3-channel PWM-controller 
	with complementary outputs and general-purpose timer TIM4 as phase-controler)	
	!warning! 
	Unfortunately this program written for testing BLDC-motors without any feedback sensors (Position Hall-sensor or current sensors) 
	and does not use approximately sinus PWM formula for correct phase and current control yet. 
	It will be fixed later. And version with feedback will be added separately. If you see this warning then I am working on it! :)
	!warning!
	
	Used STM pins:
		PD0, PD1 - 8MHz crystal oscillator
		PA0 - button (active negative signal)
		PA8-PA10 - positive phase-correct PWM signal for PWM-controller
		PB13-PB15 - negative phase-correct PWM signal for PWM-controller
*/

#include <stm32f10x.h>

/* declarate functions */
void RCCPLL_Init(void);																				
void PWM_Init(void);
void Button_Init(void);

/* declarate constants and variables */
const uint16_t BLDCinstalledRPM[6][2] = {{10000,0},
																				{5000,449},
																				{2500,549},
																				{2000,649},
																				{1500,749},
																				{1000,849}}; 		

static _Bool DIR = 0, BLDCRPMSet = 0;							
static uint8_t BLDCStep = 0, BLDCRPMMODE = 0;			

/* main program */
int main()
{
	RCCPLL_Init();																							//Clock configuration
	Button_Init();																							//Button configuration
	PWM_Init();																									//Timers configuration
	while(1)																										//infinite loop
		__WFI();												
}

/* Definition clock configuration function */
void RCCPLL_Init(void)
{
	RCC->CR |= RCC_CR_HSEON;																		//Switching-on extended clock source
	while(!(RCC->CR & RCC_CR_HSERDY))														//HSE readiness waiting loop
		__NOP();
	RCC->CFGR = RCC_CFGR_PLLMULL9 | RCC_CFGR_PPRE1_DIV2;				//Configuring PLL for multiply HSE by 9 to 72 MHz and divide APB1 periph clocks by 2 to 36 MHz according by reference manual
	RCC->CR |= RCC_CR_PLLON;																		//Switching-on PLL module
	while(!(RCC->CR & RCC_CR_PLLRDY))														//PLL readiness waiting loop
		__NOP();
	RCC->CFGR |= RCC_CFGR_SW_PLL;																//Switching system clock source on PLL
	while(!(RCC->CFGR & RCC_CFGR_SWS_PLL))											//Sysclock readiness waiting loop
		__NOP();
	RCC->CR &= ~RCC_CR_HSION;																		//Switching-off internal clock source
}

/* Definition button configuration function */
void Button_Init(void)
{
	RCC->APB2ENR = RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN;			//Switching-on Alternate-Function extended interrupt and GPIOA modules
	
	NVIC_EnableIRQ(EXTI0_IRQn);																	//Switching-on extended interrupt
	GPIOA->CRL = GPIO_CRL_CNF0_1;																//Configure PA0 for input pin with pull-up
	GPIOA->ODR = GPIO_ODR_ODR0;																	//Switching-on pull-up for PA0
	EXTI->IMR = EXTI_IMR_MR0;																		//Configure External interrupt mask for 0 line
	EXTI->FTSR = EXTI_FTSR_TR0;																	//Configure External interrupt PA0 for falling edge trigger
}

/* Definition timers configuration function */
void PWM_Init(void)
{
	RCC->APB2ENR = 	RCC_APB2ENR_TIM1EN |												//Switching-on timer 1 and GPIOA modules
									RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;

	GPIOA->CRH = 	GPIO_CRH_CNF8_1 | GPIO_CRH_MODE8 |						//Configure PA8-PA10 for Alternate-Function push-pull outputs 
								GPIO_CRH_CNF9_1 | GPIO_CRH_MODE9 |
								GPIO_CRH_CNF10_1 | GPIO_CRH_MODE10;
	GPIOB->CRH = 	GPIO_CRH_CNF13_1 | GPIO_CRH_MODE13 | 					//Configure PB13-PB15 for Alternate-Function push-pull outputs 
								GPIO_CRH_CNF14_1 | GPIO_CRH_MODE14 | 
								GPIO_CRH_CNF15_1 | GPIO_CRH_MODE15;
	
	TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 |					//Configure timer 1 for generate positive PWM signal for 3 output channels
								TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 ;
	TIM1->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 ;
	TIM1->BDTR = TIM_BDTR_MOE | TIM_BDTR_OSSR;									//Configure run-mode outputs
	TIM1->PSC = 0;																							//Timer 1 counter prescaler is 0, for high frequency PWM signal
	TIM1->ARR = 3599;																						//Timer 1 counter period (72 MHz / 3600 = 20 kHz PWM)
	TIM1->CCR1 = 0;																							//Timer 1 start duty cycle for 1st PWM channel
	TIM1->CCR2 = 0;																							//Timer 1 start duty cycle for 2nd PWM channel
	TIM1->CCR3 = 0;																							//Timer 1 start duty cycle for 3rd PWM channel
	TIM1->CR1 = TIM_CR1_ARPE;																		/*Configure timer 1 for auro-reload preload (if CCR1 or another will be changed 
																																then won't work immediately, but will work after PWM cycle)*/
	
	RCC->APB1ENR = RCC_APB1ENR_TIM4EN;													//Switching-on timer 4 module
	
	NVIC_EnableIRQ(TIM4_IRQn);																	//Switching-on timer 4 interrupt
	TIM4->DIER = TIM_DIER_UIE;																	//Switching-on timer 4 counter update interrupt
	TIM4->PSC = 35;																							//Timer 4 counter prescaler is 36
	TIM4->ARR = 65000;																					//Timer 4 start counter period (36 MHz / 36 / 65000 / 6 * 60 = 150 BLDC start PRM)
	TIM4->CR1 = TIM_CR1_ARPE;																		//Configure timer 4 for auro-reload preload
}

/* Definition Extended interrupt button function */
void EXTI0_IRQHandler(void)																		
{
	for (uint16_t i = 0; i < 0xFFFF; i++);											//Program contact bounce counteraction
	EXTI->PR = EXTI_PR_PR0;																			//Clear interrupt flag
	BLDCRPMMODE++;																							//increment motor mode
	BLDCRPMSet = 0;																							//Clear acceleration flag
	if (BLDCRPMMODE >= 6)																				//Check motor mode overflow
	{
		BLDCRPMMODE = 0;																					//Clear motor mode(Stop PWM generation)
		TIM4->CR1 &= ~TIM_CR1_CEN;																//Switching-off timer 4
		TIM1->CR1 &= ~TIM_CR1_CEN;																//Switching-off timer 1
		TIM1->CCER = 0;																						//Switching-off timer 1 outputs
		TIM4->ARR = 65000;																				//Return timer 4 start counter period
	}
	else
	{
		TIM1->CR1 |= TIM_CR1_CEN;																	//Switching-on timer 1
		TIM4->CR1 |= TIM_CR1_CEN;																	//Switching-on timer 4
	}
}

/* Definition Extended interrupt button function */
void TIM4_IRQHandler(void)
{
	TIM4->SR = 0;																								//Clear interrupt flag
	switch (BLDCStep)
	{
		case 0:																																		//Setup for first BLDC-motor step
				TIM1->CCER = 	TIM_CCER_CC1E |	TIM_CCER_CC1NP | TIM_CCER_CC1NE |					//1st channel 2 same PWM for positive and negative outputs
										//TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE |					//2nd channel is low for both outputs
										/*TIM_CCER_CC3E |*/	TIM_CCER_CC3NP; //| TIM_CCER_CC3NE;			//3rd channel positive output is low, negative is high
		break;
		
		case 1:																																		//Setup for second BLDC-motor step
				TIM1->CCER = 	//TIM_CCER_CC1E | TIM_CCER_CC1NP | TIM_CCER_CC1NE | 			//1st channel is low for both outputs
										TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE |						//2nd channel 2 same PWM for positive and negative outputs
										/*TIM_CCER_CC3E |*/	TIM_CCER_CC3NP; //| TIM_CCER_CC3NE;			//3rd channel positive output is low, negative is high
		break;
		
		case 2:																																		//Setup for third BLDC-motor step
			TIM1->CCER = 	/*TIM_CCER_CC1E |*/	TIM_CCER_CC1NP | //TIM_CCER_CC1NE | 		//1st channel positive output is low, negative is high
										TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE;						//2nd channel 2 same PWM for positive and negative outputs
										//TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;					//3rd channel is low for both outputs
		break;
		
		case 3:																																		//Setup for fourth BLDC-motor step
			TIM1->CCER = 	/*TIM_CCER_CC1E |*/	TIM_CCER_CC1NP |// TIM_CCER_CC1NE |			//1st channel positive output is low, negative is high
										//TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE |					//2nd channel is low for both outputs
										TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;						//3rd channel 2 same PWM for positive and negative outputs
		break;
		
		case 4:																																		//Setup for fifth BLDC-motor step
			TIM1->CCER = 	//TIM_CCER_CC1E | TIM_CCER_CC1NP | TIM_CCER_CC1NE | 				//1st channel is low for both outputs
										/*TIM_CCER_CC2E |*/	TIM_CCER_CC2NP |// TIM_CCER_CC2NE |			//2nd channel positive output is low, negative is high
										TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;						//3rd channel 2 same PWM for positive and negative outputs
		break;
		
		case 5:																																		//Setup for sixth BLDC-motor step
			TIM1->CCER = 	TIM_CCER_CC1E | TIM_CCER_CC1NP | TIM_CCER_CC1NE | 					//1st channel 2 same PWM for positive and negative outputs
										/*TIM_CCER_CC2E |*/	TIM_CCER_CC2NP ;// TIM_CCER_CC2NE |			//2nd channel positive output is low, negative is high
										//TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;					//3rd channel is low for both outputs
		break;
	}
	if (!BLDCRPMSet)																							//Acceleration condition
	{
		if (TIM4->ARR != BLDCinstalledRPM[BLDCRPMMODE][0])					//if motor is running
		{
			if (TIM4->ARR > 2500)																			//if motor is starting
				TIM4->ARR-= 500;																				//then fast decrement BLDC step period
			else
				TIM4->ARR--;																						//else slow decrement BLDC step period
		}
		else
			BLDCRPMSet = 1;																						//else acceleration flag is set
		if (TIM1->CCR1 != BLDCinstalledRPM[BLDCRPMMODE][1])					//Control coil current via PWM
		{
			TIM1->CCR1 = BLDCinstalledRPM[BLDCRPMMODE][1];						//set duty cycles for PWM
			TIM1->CCR2 = BLDCinstalledRPM[BLDCRPMMODE][1];
			TIM1->CCR3 = BLDCinstalledRPM[BLDCRPMMODE][1];
		}
	}
	BLDCStep++;																										//increace BLDC step
	if (BLDCStep == 6)																						//Overflow condition
		BLDCStep = 0;
}

