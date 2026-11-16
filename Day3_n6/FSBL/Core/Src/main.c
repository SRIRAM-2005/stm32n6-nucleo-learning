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
/* ======================= Day 3: timers, PWM, cache =======================
 * 1 = TIM6 interrupt every 1 s toggles red LED; period measured in CPU cycles
 * 2 = TIM12 PWM, fixed brightness: green 5 %, blue 50 %
 * 3 = TIM12 PWM "breathing": green and blue fade in opposite phase
 * 4 = Instruction cache OFF vs ON: same benchmarks, compare cycles
 * 5 = Data cache test: no caches -> D-cache only -> I-cache + D-cache
 * In every exercise TIM6 also blinks the red LED as a hardware heartbeat.
 * ========================================================================== */
#define DAY3_EXERCISE   5

#define BREATH_STEP_MS  4U     /* exercise 3: ms between brightness steps */
#define BENCH_N         1000U  /* exercise 4: repetitions per measurement */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim12;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* Written in the TIM6 interrupt, read in main(): must be volatile */
static volatile uint32_t g_tim6_events   = 0U;
static volatile uint32_t g_tim6_last_cyc = 0U;
static volatile uint32_t g_tim6_period   = 0U;   /* cycles between interrupts */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM6_Init(void);
static void MX_TIM12_Init(void);
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
static void DWT_Init(void)
{
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ------------------------ Timer configuration report ---------------------- */
/* f_update = f_timer / ((PSC + 1) * (ARR + 1))                               */
static void Print_TimerInfo(void)
{
  uint32_t ftim = HAL_RCCEx_GetTIMGFreq();
  uint32_t psc6 = TIM6->PSC,  arr6 = TIM6->ARR;
  uint32_t psc12 = TIM12->PSC, arr12 = TIM12->ARR;

  printf("Timer kernel clock (TIMG) = %lu Hz\r\n", (unsigned long)ftim);
  printf("TIM6 : PSC=%lu ARR=%lu -> counter %lu Hz, update %lu.%03lu Hz\r\n",
         (unsigned long)psc6, (unsigned long)arr6,
         (unsigned long)(ftim / (psc6 + 1U)),
         (unsigned long)((ftim / (psc6 + 1U)) / (arr6 + 1U)),
         (unsigned long)((((ftim / (psc6 + 1U)) % (arr6 + 1U)) * 1000U) / (arr6 + 1U)));
  printf("TIM12: PSC=%lu ARR=%lu -> PWM frequency %lu Hz, %lu steps\r\n",
         (unsigned long)psc12, (unsigned long)arr12,
         (unsigned long)((ftim / (psc12 + 1U)) / (arr12 + 1U)),
         (unsigned long)(arr12 + 1U));
}

/* ------------------------------ TIM6 interrupt ---------------------------- */
/* Called by HAL_TIM_IRQHandler() from TIM6_IRQHandler (stm32n6xx_it.c).      */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    uint32_t now = DWT->CYCCNT;
    if (g_tim6_events > 0U)
    {
      g_tim6_period = now - g_tim6_last_cyc;
    }
    g_tim6_last_cyc = now;
    g_tim6_events++;
    HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);   /* heartbeat */
  }
}

/* ----------------------------- PWM duty helper ---------------------------- */
/* LEDs are active low; CubeMX sets channel polarity LOW, so CCR = ON time.   */
static void Pwm_SetPermille(uint32_t channel, uint32_t permille)
{
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim12);
  uint32_t ccr = ((arr + 1U) * permille) / 1000U;
  __HAL_TIM_SET_COMPARE(&htim12, channel, ccr);
}

/* ============================== Exercise 1 =============================== */
__attribute__((unused)) static void Run_Exercise1(void)
{
  uint32_t seen = 0U;
  printf("TIM6 interrupt toggles the red LED. Period measured with DWT:\r\n");
  while (1)
  {
    if (g_tim6_events != seen)
    {
      seen = g_tim6_events;
      if (seen >= 2U)
      {
        uint32_t p = g_tim6_period;
        printf("tick %3lu  period = %lu cycles = %lu.%06lu s\r\n",
               (unsigned long)seen, (unsigned long)p,
               (unsigned long)(p / SystemCoreClock),
               (unsigned long)(((uint64_t)(p % SystemCoreClock) * 1000000ULL) / SystemCoreClock));
      }
    }
  }
}

/* ============================== Exercise 2 =============================== */
__attribute__((unused)) static void Run_Exercise2(void)
{
  Pwm_SetPermille(TIM_CHANNEL_1, 50U);    /* green  5.0 % */
  Pwm_SetPermille(TIM_CHANNEL_2, 500U);   /* blue  50.0 % */
  printf("Green (PG0, TIM12_CH1): CCR1=%lu of %lu -> 5%% on\r\n",
         (unsigned long)TIM12->CCR1, (unsigned long)(TIM12->ARR + 1U));
  printf("Blue  (PG8, TIM12_CH2): CCR2=%lu of %lu -> 50%% on\r\n",
         (unsigned long)TIM12->CCR2, (unsigned long)(TIM12->ARR + 1U));
  printf("The CPU now does nothing: the timer drives the pins by itself.\r\n");
  while (1) { }
}

/* ============================== Exercise 3 =============================== */
__attribute__((unused)) static void Run_Exercise3(void)
{
  uint32_t x = 0U;          /* 0..1000 position in the fade */
  int32_t  dir = 1;
  uint32_t last = HAL_GetTick();

  printf("Breathing: green and blue fade in opposite phase.\r\n");
  while (1)
  {
    if ((HAL_GetTick() - last) >= BREATH_STEP_MS)
    {
      last = HAL_GetTick();
      /* Eyes see brightness roughly logarithmically: square the ramp so the
       * fade looks even (simple gamma correction).                          */
      uint32_t g = (x * x) / 1000U;
      uint32_t b = ((1000U - x) * (1000U - x)) / 1000U;
      Pwm_SetPermille(TIM_CHANNEL_1, g);
      Pwm_SetPermille(TIM_CHANNEL_2, b);

      if ((dir > 0) && (x >= 1000U)) { dir = -1; }
      if ((dir < 0) && (x == 0U))    { dir =  1; }
      x = (uint32_t)((int32_t)x + dir * 5);
    }
  }
}

/* ============================== Exercise 4 =============================== */
static uint32_t bench_cycles(void (*fn)(void))
{
  uint32_t t0, t1;
  __disable_irq();
  t0 = DWT->CYCCNT;
  fn();
  t1 = DWT->CYCCNT;
  __enable_irq();
  return t1 - t0;
}

static void bench_hal_write(void)
{
  for (uint32_t i = 0U; i < BENCH_N; i++)
  {
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
  }
}

static volatile uint32_t g_sink;
static void bench_compute(void)
{
  uint32_t acc = 1U;
  for (uint32_t i = 0U; i < BENCH_N; i++)
  {
    acc = acc * 1664525U + 1013904223U;    /* pure CPU work, no peripherals */
    acc ^= (acc >> 7);
  }
  g_sink = acc;
}

static void bench_report(const char *label)
{
  uint32_t hal = bench_cycles(bench_hal_write);
  uint32_t cpu = bench_cycles(bench_compute);
  printf("%-10s  HAL_GPIO_WritePin: %4lu cycles/call   compute loop: %4lu cycles/iter\r\n",
         label, (unsigned long)(hal / BENCH_N), (unsigned long)(cpu / BENCH_N));
}

__attribute__((unused)) static void Run_Exercise4(void)
{
  HAL_NVIC_DisableIRQ(TIM6_IRQn);     /* keep the heartbeat out of the timing */

  printf("I-cache at start: %s\r\n", (SCB->CCR & SCB_CCR_IC_Msk) ? "ON" : "OFF");
  bench_report("cache OFF");
  bench_report("cache OFF");          /* run twice: results must be stable */

  SCB_EnableICache();
  printf("SCB_EnableICache() called -> I-cache %s\r\n",
         (SCB->CCR & SCB_CCR_IC_Msk) ? "ON" : "OFF");
  bench_report("cache ON");           /* first run fills the cache */
  bench_report("cache ON");           /* second run: everything cached */

  HAL_NVIC_EnableIRQ(TIM6_IRQn);
  while (1) { }
}

__attribute__((unused)) static void Run_Exercise5(void)
{
  HAL_NVIC_DisableIRQ(TIM6_IRQn);     /* keep the heartbeat out of the timing */

  printf("I-cache %s, D-cache %s at start\r\n",
         (SCB->CCR & SCB_CCR_IC_Msk) ? "ON" : "OFF",
         (SCB->CCR & SCB_CCR_DC_Msk) ? "ON" : "OFF");
  bench_report("no cache");
  bench_report("no cache");

  /* D-cache only. Safe here because nothing uses DMA: with DMA, cached
   * buffers must be cleaned/invalidated or the CPU and DMA see different
   * data (cache coherence).                                                 */
  SCB_EnableDCache();
  printf("SCB_EnableDCache() -> D-cache %s\r\n",
         (SCB->CCR & SCB_CCR_DC_Msk) ? "ON" : "OFF");
  bench_report("D only");
  bench_report("D only");

  SCB_EnableICache();
  printf("SCB_EnableICache() -> I-cache %s\r\n",
         (SCB->CCR & SCB_CCR_IC_Msk) ? "ON" : "OFF");
  bench_report("I + D");
  bench_report("I + D");

  HAL_NVIC_EnableIRQ(TIM6_IRQn);
  while (1) { }
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

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_TIM6_Init();
  MX_TIM12_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);
  DWT_Init();

  printf("\r\n=== NUCLEO-N657X0-Q Day 3: timers, PWM, cache ===\r\n");
  printf("SystemCoreClock = %lu Hz, exercise %d\r\n",
         (unsigned long)SystemCoreClock, DAY3_EXERCISE);
  Print_TimerInfo();

  /* Start the hardware: TIM6 with its update interrupt, TIM12 PWM channels */
  Pwm_SetPermille(TIM_CHANNEL_1, 0U);
  Pwm_SetPermille(TIM_CHANNEL_2, 0U);
  if (HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1) != HAL_OK) { Error_Handler(); }
  if (HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2) != HAL_OK) { Error_Handler(); }
  if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK)             { Error_Handler(); }

#if   (DAY3_EXERCISE == 1)
  Run_Exercise1();
#elif (DAY3_EXERCISE == 2)
  Run_Exercise2();
#elif (DAY3_EXERCISE == 3)
  Run_Exercise3();
#elif (DAY3_EXERCISE == 4)
  Run_Exercise4();
#elif (DAY3_EXERCISE == 5)
  Run_Exercise5();
#else
#error "DAY3_EXERCISE must be 1, 2, 3, 4 or 5"
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
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_TIM;
  PeriphClkInitStruct.TIMPresSelection = RCC_TIMPRES_DIV1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 39999;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 9999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief TIM12 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM12_Init(void)
{

  /* USER CODE BEGIN TIM12_Init 0 */

  /* USER CODE END TIM12_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM12_Init 1 */

  /* USER CODE END TIM12_Init 1 */
  htim12.Instance = TIM12;
  htim12.Init.Prescaler = 399;
  htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim12.Init.Period = 999;
  htim12.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim12) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim12, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim12, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM12_Init 2 */

  /* USER CODE END TIM12_Init 2 */
  HAL_TIM_MspPostInit(&htim12);

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
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);

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

  /*Configure GPIO pin : LED_RED_Pin */
  GPIO_InitStruct.Pin = LED_RED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_RED_GPIO_Port, &GPIO_InitStruct);

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
