/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ===================== Day 2: registers, cycles, clocks =====================
 * 1 = Blue LED (PG8) blink set up 100% with registers, DWT-based delay
 * 2 = Button (PC13) read from IDR register, mirrored on blue LED via BSRR
 * 3 = Benchmark: CPU cycles for HAL calls vs direct register writes
 * 4 = Clock tree report + SysTick registers + measure 1 s with cycle counter
 * ========================================================================== */
#define DAY2_EXERCISE   3

/* Blue LED LD7 = PG8, active low (UM3417). NOT configured in CubeMX on
 * purpose: everything about this pin is done by hand below. */
#define BLUE_PIN        8U

/* Button B1 = PC13, active high. CubeMX already configured it (EXTI). */
#define BTN_PIN         13U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ---------------------------- printf -> USART1 ---------------------------- */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  (void)HAL_UART_Transmit(&huart1, &c, 1U, HAL_MAX_DELAY);
  return ch;
}

/* ------------------------- DWT cycle counter (M55) ------------------------ */
/* DWT->CYCCNT counts every CPU clock cycle. At 600 MHz: 1 cycle = 1.67 ns,
 * and the 32-bit counter wraps every ~7.16 s.                               */
static void DWT_Init(void)
{
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;     /* power up the trace/debug block   */
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;   /* start counting                   */
}

static inline uint32_t cycles_now(void)
{
  return DWT->CYCCNT;
}

/* Busy-wait using the cycle counter. Unsigned subtraction is wrap-safe. */
static void delay_ms_dwt(uint32_t ms)
{
  const uint32_t start = cycles_now();
  const uint32_t ticks = ms * (SystemCoreClock / 1000U);
  while ((cycles_now() - start) < ticks) { }
}

/* --------------------------- Blue LED by registers ------------------------ */
static void BlueLed_InitRegisters(void)
{
  /* 1. Clock: AHB4ENSR is a "set" register - writing 1 enables that clock,
   *    writing 0 changes nothing, so no read-modify-write is needed.       */
  RCC->AHB4ENSR = RCC_AHB4ENSR_GPIOGENS;
  (void)RCC->AHB4ENR;                     /* read back: make sure it's done */

  /* 2. Output level first (HIGH = LED off), so the pin never glitches on.  */
  GPIOG->BSRR = (1UL << BLUE_PIN);

  /* 3. MODER: 2 bits per pin. 00 input, 01 output, 10 AF, 11 analog.      */
  GPIOG->MODER   &= ~(3UL << (BLUE_PIN * 2U));
  GPIOG->MODER   |=  (1UL << (BLUE_PIN * 2U));

  /* 4. OTYPER: 1 bit per pin. 0 = push-pull.                               */
  GPIOG->OTYPER  &= ~(1UL << BLUE_PIN);

  /* 5. OSPEEDR: 2 bits per pin. 00 = low speed (enough for an LED).        */
  GPIOG->OSPEEDR &= ~(3UL << (BLUE_PIN * 2U));

  /* 6. PUPDR: 2 bits per pin. 00 = no pull-up / pull-down.                 */
  GPIOG->PUPDR   &= ~(3UL << (BLUE_PIN * 2U));
}

/* BSRR: lower 16 bits SET pins, upper 16 bits RESET pins. One write, no
 * read-modify-write, so it can't be corrupted by an interrupt.             */
static inline void BlueLed_On(void)  { GPIOG->BSRR = (1UL << (BLUE_PIN + 16U)); } /* LOW  */
static inline void BlueLed_Off(void) { GPIOG->BSRR = (1UL << BLUE_PIN); }         /* HIGH */

static void Print_GpioRegisters(void)
{
  printf("GPIOG->MODER   = 0x%08lX  (PG8 bits[17:16] = %lu, expect 1 = output)\r\n",
         (unsigned long)GPIOG->MODER,
         (unsigned long)((GPIOG->MODER >> (BLUE_PIN * 2U)) & 3UL));
  printf("GPIOG->OTYPER  = 0x%08lX\r\n", (unsigned long)GPIOG->OTYPER);
  printf("GPIOG->OSPEEDR = 0x%08lX\r\n", (unsigned long)GPIOG->OSPEEDR);
  printf("GPIOG->PUPDR   = 0x%08lX\r\n", (unsigned long)GPIOG->PUPDR);
  printf("GPIOG->ODR     = 0x%08lX\r\n", (unsigned long)GPIOG->ODR);
  printf("GPIOC->MODER   = 0x%08lX  (PC13 bits[27:26] = %lu, expect 0 = input)\r\n",
         (unsigned long)GPIOC->MODER,
         (unsigned long)((GPIOC->MODER >> (BTN_PIN * 2U)) & 3UL));
  printf("GPIOC->PUPDR   = 0x%08lX  (PC13 bits[27:26] = %lu, expect 2 = pull-down)\r\n",
         (unsigned long)GPIOC->PUPDR,
         (unsigned long)((GPIOC->PUPDR >> (BTN_PIN * 2U)) & 3UL));
}

/* ============================== Exercise 1 =============================== */
__attribute__((unused)) static void Run_Exercise1(void)
{
  uint32_t n = 0U;
  printf("Blue LED (PG8) blinking, set up with registers only.\r\n");
  while (1)
  {
    BlueLed_On();
    delay_ms_dwt(200U);
    BlueLed_Off();
    delay_ms_dwt(800U);
    printf("blink %lu  ODR bit8=%lu\r\n", (unsigned long)++n,
           (unsigned long)((GPIOG->ODR >> BLUE_PIN) & 1UL));
  }
}

/* ============================== Exercise 2 =============================== */
__attribute__((unused)) static void Run_Exercise2(void)
{
  uint32_t prev = 2U;    /* impossible value, forces the first print */
  printf("Hold the blue button: blue LED follows it (IDR -> BSRR).\r\n");
  while (1)
  {
    uint32_t pressed = (GPIOC->IDR >> BTN_PIN) & 1UL;   /* 1 = pressed */
    if (pressed) { BlueLed_On(); } else { BlueLed_Off(); }

    if (pressed != prev)
    {
      prev = pressed;
      printf("GPIOC->IDR = 0x%04lX  -> button %s\r\n",
             (unsigned long)(GPIOC->IDR & 0xFFFFUL),
             pressed ? "PRESSED" : "released");
    }
  }
}

/* ============================== Exercise 3 =============================== */
#define BENCH_N  1000U

__attribute__((unused)) static void Run_Exercise3(void)
{
  uint32_t t0, t1, i;
  uint32_t empty, hal_write, hal_toggle, bsrr, odr_rmw;

  printf("Average CPU cycles per operation (%u runs, 1 cycle = %lu ps)\r\n",
         BENCH_N, (unsigned long)(1000000000UL / (SystemCoreClock / 1000U)));

  __disable_irq();   /* keep SysTick/EXTI out of the measurement */

  t0 = cycles_now();
  for (i = 0U; i < BENCH_N; i++) { __NOP(); }
  t1 = cycles_now();
  empty = t1 - t0;

  t0 = cycles_now();
  for (i = 0U; i < BENCH_N; i++) { HAL_GPIO_WritePin(GPIOG, GPIO_PIN_8, GPIO_PIN_SET); }
  t1 = cycles_now();
  hal_write = t1 - t0;

  t0 = cycles_now();
  for (i = 0U; i < BENCH_N; i++) { HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_8); }
  t1 = cycles_now();
  hal_toggle = t1 - t0;

  t0 = cycles_now();
  for (i = 0U; i < BENCH_N; i++) { GPIOG->BSRR = (1UL << BLUE_PIN); }
  t1 = cycles_now();
  bsrr = t1 - t0;

  t0 = cycles_now();
  for (i = 0U; i < BENCH_N; i++) { GPIOG->ODR ^= (1UL << BLUE_PIN); }
  t1 = cycles_now();
  odr_rmw = t1 - t0;

  __enable_irq();
  BlueLed_Off();

  /* subtract the loop's own overhead */
  printf("  loop overhead only       : %4lu\r\n", (unsigned long)(empty / BENCH_N));
  printf("  HAL_GPIO_WritePin        : %4lu\r\n", (unsigned long)((hal_write  - empty) / BENCH_N));
  printf("  HAL_GPIO_TogglePin       : %4lu\r\n", (unsigned long)((hal_toggle - empty) / BENCH_N));
  printf("  GPIOG->BSRR = mask       : %4lu\r\n", (unsigned long)((bsrr       - empty) / BENCH_N));
  printf("  GPIOG->ODR ^= mask (RMW) : %4lu\r\n", (unsigned long)((odr_rmw    - empty) / BENCH_N));
  printf("Build is -O0 (Debug). Try Release / -O2 and compare.\r\n");

  while (1) { }
}

/* ============================== Exercise 4 =============================== */
__attribute__((unused)) static void Run_Exercise4(void)
{
  uint32_t load = SysTick->LOAD;
  uint32_t ctrl = SysTick->CTRL;
  uint32_t t0, t1;

  printf("--- Clock tree (from RCC registers via HAL) ---\r\n");
  printf("  CPU   (SYSA) : %lu Hz\r\n", (unsigned long)HAL_RCC_GetCpuClockFreq());
  printf("  SYSCLK(SYSB) : %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
  printf("  HCLK  (AHB)  : %lu Hz\r\n", (unsigned long)HAL_RCC_GetHCLKFreq());
  printf("  PCLK1 (APB1) : %lu Hz\r\n", (unsigned long)HAL_RCC_GetPCLK1Freq());
  printf("  PCLK2 (APB2) : %lu Hz  <- USART1 runs from this bus\r\n",
         (unsigned long)HAL_RCC_GetPCLK2Freq());
  printf("  PCLK4 (APB4) : %lu Hz\r\n", (unsigned long)HAL_RCC_GetPCLK4Freq());
  printf("  PCLK5 (APB5) : %lu Hz\r\n", (unsigned long)HAL_RCC_GetPCLK5Freq());

  printf("--- SysTick registers ---\r\n");
  printf("  SysTick->LOAD = %lu  -> interrupt every %lu cycles\r\n",
         (unsigned long)load, (unsigned long)(load + 1U));
  printf("  SysTick->CTRL = 0x%lX  (ENABLE=%lu TICKINT=%lu CLKSOURCE=%lu)\r\n",
         (unsigned long)ctrl,
         (unsigned long)(ctrl & SysTick_CTRL_ENABLE_Msk),
         (unsigned long)((ctrl & SysTick_CTRL_TICKINT_Msk) >> SysTick_CTRL_TICKINT_Pos),
         (unsigned long)((ctrl & SysTick_CTRL_CLKSOURCE_Msk) >> SysTick_CTRL_CLKSOURCE_Pos));
  printf("  tick rate = CPU / (LOAD+1) = %lu Hz\r\n",
         (unsigned long)(HAL_RCC_GetCpuClockFreq() / (load + 1U)));

  printf("--- Measuring HAL_Delay(1000) with the cycle counter ---\r\n");
  t0 = cycles_now();
  HAL_Delay(1000);
  t1 = cycles_now();
  printf("  cycles = %lu  (expect ~%lu)\r\n",
         (unsigned long)(t1 - t0), (unsigned long)SystemCoreClock);

  while (1)
  {
    BlueLed_On();  HAL_Delay(100);
    BlueLed_Off(); HAL_Delay(900);
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);
  DWT_Init();
  BlueLed_InitRegisters();

  printf("\r\n=== NUCLEO-N657X0-Q Day 2: registers ===\r\n");
  printf("SystemCoreClock = %lu Hz, exercise %d\r\n",
         (unsigned long)SystemCoreClock, DAY2_EXERCISE);
  Print_GpioRegisters();

#if   (DAY2_EXERCISE == 1)
  Run_Exercise1();
#elif (DAY2_EXERCISE == 2)
  Run_Exercise2();
#elif (DAY2_EXERCISE == 3)
  Run_Exercise3();
#elif (DAY2_EXERCISE == 4)
  Run_Exercise4();
#else
#error "DAY2_EXERCISE must be 1, 2, 3 or 4"
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}
/* USER CODE BEGIN CLK 1 */
/* USER CODE END CLK 1 */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the System Power Supply
  */
  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Enable HSI */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Get current CPU/System buses clocks configuration and if necessary switch
 to intermediate HSI clock to ensure target clock can be set
  */
  HAL_RCC_GetClockConfig(&RCC_ClkInitStruct);
  if ((RCC_ClkInitStruct.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
     (RCC_ClkInitStruct.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11))
  {
    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK);
    RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
    {
      /* Initialization Error */
      Error_Handler();
    }
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL1.PLLM = 4;
  RCC_OscInitStruct.PLL1.PLLN = 75;
  RCC_OscInitStruct.PLL1.PLLFractional = 0;
  RCC_OscInitStruct.PLL1.PLLP1 = 1;
  RCC_OscInitStruct.PLL1.PLLP2 = 1;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_CPUCLK|RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2|RCC_CLOCKTYPE_PCLK5
                              |RCC_CLOCKTYPE_PCLK4;
  RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV1;
  RCC_ClkInitStruct.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC1Selection.ClockDivider = 2;
  RCC_ClkInitStruct.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC2Selection.ClockDivider = 3;
  RCC_ClkInitStruct.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC6Selection.ClockDivider = 4;
  RCC_ClkInitStruct.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC11Selection.ClockDivider = 3;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, LED_GREEN_Pin|LED_RED_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : I2C1_SDA_Pin */
  GPIO_InitStruct.Pin = I2C1_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(I2C1_SDA_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : I2CA_SCL_Pin */
  GPIO_InitStruct.Pin = I2CA_SCL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(I2CA_SCL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USER_BTN_Pin */
  GPIO_InitStruct.Pin = USER_BTN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(USER_BTN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : I2C2_SDA_Pin I2C2_SCL_Pin */
  GPIO_InitStruct.Pin = I2C2_SDA_Pin|I2C2_SCL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PA10 UCPD1_VSENSE_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_10|UCPD1_VSENSE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_GREEN_Pin LED_RED_Pin */
  GPIO_InitStruct.Pin = LED_GREEN_Pin|LED_RED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure the EXTI line attribute */
  HAL_EXTI_ConfigLineAttributes(EXTI_LINE_13, EXTI_LINE_SEC);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(USER_BTN_EXTI_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USER_BTN_EXTI_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Fast red blink forever = an init step failed */
  __disable_irq();
  while (1)
  {
    HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
    for (volatile uint32_t i = 0U; i < 2000000U; i++) { }
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
