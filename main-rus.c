#include <stm32f10x.h>																				//Библиотека для stm32f103
#define PI 3.1415926535897932384626433832795f									//Константа для floatPI из armmath.h

/* объявление функций */
void RCCPLL_Init(void);																				
void CAN_Init(void);
void PWM_Init(void);
void Button_Init(void);

/* объявление структуры */
typedef struct{
	float angular_velosity;
	float angular_position;
	uint64_t timestamp;
}Submodule_data;

static Submodule_data SubmoduleStruct;

/* таблица констант для bldc */
const uint16_t BLDCinstalledRPM[6][2] = {{10000,0},
																				{5000,449},
																				{2500,549},
																				{2000,649},
																				{1500,749},
																				{1000,849}}; 		//72МГц - Fclk тактовая частота ядра
																									//Ftim1 = Fclk - Тактовая частота таймера
																									//Fcnt = Ftim1 / 1 +(TIM1->PSC = 71) = 72000000 / 1 + 71 = 1 МГц - Частота счета таймера с учетом предделителя
																									//Fstep = Fcnt / 1 + (TIM1->ARR = 999(пример)) = 1000000 / 1 + 999 = 1 кГц - Установка периода для одного шага BLDC двигателя
																									//Fbldc = Fstep / 6 = 166,66666 Гц - "Частота" оборотов двигателя
																									//Frpm = Fbldc * 60 = 10000 - Обороты двигателя в минуту

static _Bool DIR = 0, BLDCRPMSet = 0;							//флаги направления и установленной скорости
static uint8_t BLDCStep = 0, BLDCRPMMODE = 0;			//переменные положения двигателя и установленной скорости


int main()
{
	RCCPLL_Init();										//установка тектовой частоты 72 МГц
	Button_Init();										//Инициализация внешнего прерывания по кнопке
	CAN_Init();												//Инициализация CAN интерфейса
	PWM_Init();												//Инициализация всех ШИМ
	while(1)
	{
		__WFI();												//Ожидание прерывания
	}
}

void RCCPLL_Init(void)
{
	RCC->CR |= RCC_CR_HSEON;																		//включение внешнего тактирования
	while(!(RCC->CR & RCC_CR_HSERDY))														//Ожидание готовности
		__NOP();
	RCC->CFGR = RCC_CFGR_PLLMULL9 | RCC_CFGR_PPRE1_DIV2;				//Установка умножителей и предделителей
	RCC->CR |= RCC_CR_PLLON;																		//Включение ФАПЧ
	while(!(RCC->CR & RCC_CR_PLLRDY))														//Ожидание готовности ФАПЧ
		__NOP();
	RCC->CFGR |= RCC_CFGR_SW_PLL;																//Переключения тактовой частоты на ФАПЧ
	while(!(RCC->CFGR & RCC_CFGR_SWS_PLL))											//Ожидание переключения
		__NOP();
	RCC->CR &= ~RCC_CR_HSION;																		//Отключение внутреннего тактирования
}

void CAN_Init(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN | RCC_APB2ENR_AFIOEN;			//Включение тактирования для GPIO
	RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;																								//Включение тактирования CAN-контроллера
	
	GPIOB->CRH = GPIO_CRH_CNF8_1 | GPIO_CRH_CNF9_1 | GPIO_CRH_MODE9;									//Инициализация GPIO
	GPIOC->CRH = GPIO_CRH_MODE10 | GPIO_CRH_MODE13_1;
	GPIOC->BRR = GPIO_BRR_BR10 | GPIO_BRR_BR13;
	
	AFIO->MAPR |= AFIO_MAPR_CAN_REMAP_1;																							//Переопределение выводов CAN

	NVIC_EnableIRQ(CAN1_RX1_IRQn);																										//Включение прерывания по приёму CAN во второй FIFO
	
	CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_NART | CAN_MCR_AWUM;														//Инициализация CAN
	CAN1->BTR = /*CAN_BTR_LBKM | */0x10011;
	CAN1->IER = CAN_IER_FMPIE0 | CAN_IER_FMPIE1 | CAN_IER_TMEIE;
	CAN1->MCR &= ~((uint32_t)CAN_MCR_INRQ);
	
	CAN1->FMR = 0xE01;
	CAN1->FA1R = CAN_FA1R_FACT10;
	CAN1->FFA1R = CAN_FFA1R_FFA10;
	CAN1->FS1R = CAN_FS1R_FSC10;
	CAN1->sFilterRegister[10].FR1 = 0;
	CAN1->sFilterRegister[10].FR2 = 0;
	CAN1->FMR &= ~CAN_FMR_FINIT;
}

void Button_Init(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN;													//Включение тактирования для GPIOA
	
	NVIC_EnableIRQ(EXTI0_IRQn);
	GPIOA->CRL = GPIO_CRL_CNF0_1;
	GPIOA->ODR = GPIO_ODR_ODR0;
	EXTI->IMR = EXTI_IMR_MR0;
	EXTI->FTSR = EXTI_FTSR_TR0;
}

void PWM_Init(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;

	GPIOA->CRH = GPIO_CRH_CNF8_1 | GPIO_CRH_MODE8 | GPIO_CRH_CNF9_1 | GPIO_CRH_MODE9 | GPIO_CRH_CNF10_1 | GPIO_CRH_MODE10 | GPIO_CRH_CNF15_1 | GPIO_CRH_MODE15;
	GPIOB->CRL &= ~GPIO_CRL_CNF6;
	GPIOB->CRL = GPIO_CRL_CNF3_1 | GPIO_CRL_MODE3 | GPIO_CRL_CNF6_1 | GPIO_CRL_MODE6;
	GPIOB->CRH &= ~GPIO_CRH_CNF12;
	GPIOB->CRH = GPIO_CRH_MODE12 | GPIO_CRH_CNF13_1 | GPIO_CRH_MODE13 | GPIO_CRH_CNF14_1 | GPIO_CRH_MODE14 | GPIO_CRH_CNF15_1 | GPIO_CRH_MODE15;
	
	TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 |
								TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 ;
	TIM1->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 ;
	TIM1->BDTR = TIM_BDTR_MOE | TIM_BDTR_OSSR;
	TIM1->PSC = 0;
	TIM1->ARR = 3599;
	TIM1->CCR1 = 699;
	TIM1->CCR2 = 699;
	TIM1->CCR3 = 699;
	TIM1->CR1 = TIM_CR1_ARPE;
	
	RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
	
	NVIC_EnableIRQ(TIM4_IRQn);
	TIM4->DIER = TIM_DIER_UIE;
	TIM4->PSC = 35;
	TIM4->ARR = 65000;
	TIM4->CR1 = TIM_CR1_ARPE;
	
	
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB2ENR_AFIOEN;
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

	AFIO->MAPR = AFIO_MAPR_TIM2_REMAP_0;

	TIM2->CCMR1 |= TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 |
								 TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1;// | TIM_CCMR1_OC1PE | TIM_CCMR1_OC1FE;
	TIM2->CCER |= TIM_CCER_CC1E | TIM_CCER_CC2E;
	TIM2->DIER |= TIM_DIER_UIE;
	TIM2->PSC = 0;
	TIM2->ARR = 3599;
	TIM2->CNT = 0;
	TIM2->CCR1 = 0;
	TIM2->CCR2 = 0;
	TIM2->CR1 |= TIM_CR1_CEN; //| TIM_CR1_ARPE;
}

void EXTI0_IRQHandler(void)
{
	for (uint16_t i = 0; i < 0xFFFF; i++);
	EXTI->PR = EXTI_PR_PR0;
	BLDCRPMMODE++;
	BLDCRPMSet = 0;
	if (BLDCRPMMODE >= 6)
	{
		BLDCRPMMODE = 0;
		TIM4->CR1 &= ~TIM_CR1_CEN;
		TIM1->CR1 &= ~TIM_CR1_CEN;
		TIM1->CCER = 0;
		TIM4->ARR = 65000;
	}
	else
	{
		TIM1->CR1 |= TIM_CR1_CEN;
		TIM4->CR1 |= TIM_CR1_CEN;
	}
}

void TIM2_IRQHandler(void)
{
	TIM2->SR = 0;
	TIM2->CCR1++;
	if (TIM2->CCR1 == 1499)
		TIM2->CCR1 = 999;
}

void TIM4_IRQHandler(void)
{
	TIM4->SR = 0;
	SubmoduleStruct.timestamp++;
	SubmoduleStruct.angular_position = (PI/3)*BLDCStep;
	SubmoduleStruct.angular_velosity = 1000000/(TIM4->ARR+1)/6*2*PI;
	switch (BLDCStep)
	{
		case 0:
				TIM1->CCER = 	TIM_CCER_CC1E |	TIM_CCER_CC1NP | TIM_CCER_CC1NE |
										//TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE |
										/*TIM_CCER_CC3E |*/	TIM_CCER_CC3NP; //| TIM_CCER_CC3NE;
		break;
		
		case 1:
				TIM1->CCER = 	//TIM_CCER_CC1E | TIM_CCER_CC1NP | TIM_CCER_CC1NE | 
										TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE |
										/*TIM_CCER_CC3E |*/	TIM_CCER_CC3NP; //| TIM_CCER_CC3NE;
		break;
		
		case 2:
			TIM1->CCER = 	/*TIM_CCER_CC1E |*/	TIM_CCER_CC1NP | //TIM_CCER_CC1NE | 
										TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE;
										//TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;
		break;
		
		case 3:
			TIM1->CCER = 	/*TIM_CCER_CC1E |*/	TIM_CCER_CC1NP |// TIM_CCER_CC1NE |
										//TIM_CCER_CC2E | TIM_CCER_CC2NP | TIM_CCER_CC2NE |
										TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;
		break;
		
		case 4:
			TIM1->CCER = 	//TIM_CCER_CC1E | TIM_CCER_CC1NP | TIM_CCER_CC1NE | 
										/*TIM_CCER_CC2E |*/	TIM_CCER_CC2NP |// TIM_CCER_CC2NE |
										TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;
		break;
		
		case 5:
			TIM1->CCER = 	TIM_CCER_CC1E | TIM_CCER_CC1NP | TIM_CCER_CC1NE | 
										/*TIM_CCER_CC2E |*/	TIM_CCER_CC2NP ;// TIM_CCER_CC2NE |
										//TIM_CCER_CC3E | TIM_CCER_CC3NP | TIM_CCER_CC3NE;
		break;
	}
	if (!BLDCRPMSet)
	{
		TIM2->CCR1 = 600 * BLDCRPMMODE;
		GPIOB->ODR |= GPIO_ODR_ODR12;
		if (TIM4->ARR != BLDCinstalledRPM[BLDCRPMMODE][0])
			if (TIM4->ARR > 2500)
				TIM4->ARR-= 500;
			else
				TIM4->ARR--;
		else
			BLDCRPMSet = 1;
		if (TIM1->CCR1 != BLDCinstalledRPM[BLDCRPMMODE][1])
		{
			TIM1->CCR1 = BLDCinstalledRPM[BLDCRPMMODE][1];
			TIM1->CCR2 = BLDCinstalledRPM[BLDCRPMMODE][1];
			TIM1->CCR3 = BLDCinstalledRPM[BLDCRPMMODE][1];
		}
	}
	BLDCStep++;
	if (BLDCStep == 6)
		BLDCStep = 0;
}

void CAN1_RX1_IRQHandler(void)
{
	CAN1->RF1R = CAN_RF1R_RFOM1;
}
