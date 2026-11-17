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
#include "arm_nnfunctions.h"        /* CMSIS-NN public API        */
#include "arm_nnsupportfunctions.h" /* arm_nn_requantize() etc.   */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ================= Day 6: TinyML on the Cortex-M55 CPU ======================
 * One int8 Conv1D layer computed two ways and timed with the DWT cycle counter:
 *   (a) plain C reference, one multiply-accumulate at a time
 *   (b) CMSIS-NN arm_convolve_s8 (Helium / MVE vector kernels on the M55)
 * Each runs with caches OFF, then with I-cache + D-cache ON.
 * Outputs must match exactly (same int8 maths).
 * Runs from RAM in development boot: nothing permanent is written.
 * ========================================================================== */
#define BENCH_RUNS   5U   /* each measurement = fastest of 5 runs */

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

/* ===================== int8 Conv1D layer (TFLite-style) ======================
 * Shapes (NHWC, H = 1 because it is a 1-D signal):
 *   input   [1][1][SIG_LEN][IN_CH]       int8
 *   weights [OUT_CH][1][KERNEL][IN_CH]   int8  (per-output-channel scale)
 *   bias    [OUT_CH]                     int32
 *   output  [1][1][SIG_LEN][OUT_CH]      int8  ("same" padding, stride 1)
 * Quantization: out = clamp( requant(bias + sum((in + in_off) * w)) + out_off )
 * ========================================================================== */
#define SIG_LEN    128
#define IN_CH      8
#define OUT_CH     16
#define KERNEL     5
#define PAD        (KERNEL / 2)
#define IN_ZP      (-3)              /* input zero point  -> input_offset  = +3 */
#define OUT_ZP     (5)               /* output zero point -> output_offset = 5  */
#define LAYER_MACS ((uint32_t)SIG_LEN * OUT_CH * KERNEL * IN_CH)

static int8_t  g_in[SIG_LEN * IN_CH]                __attribute__((aligned(16)));
static int8_t  g_w[OUT_CH * KERNEL * IN_CH]         __attribute__((aligned(16)));
static int32_t g_bias[OUT_CH];
static int32_t g_mult[OUT_CH];
static int32_t g_shift[OUT_CH];
static int8_t  g_out_ref[SIG_LEN * OUT_CH]          __attribute__((aligned(16)));
static int8_t  g_out_nn[SIG_LEN * OUT_CH]           __attribute__((aligned(16)));
static int8_t  g_scratch[2048]                      __attribute__((aligned(16)));

/* Deterministic pseudo-random test data (same on every run) */
static uint32_t g_rng = 12345U;
static int8_t rnd8(void)
{
  g_rng = g_rng * 1664525U + 1013904223U;
  return (int8_t)(g_rng >> 24);
}

static void Layer_InitData(void)
{
  for (uint32_t i = 0; i < sizeof(g_in); i++) { g_in[i] = rnd8(); }
  for (uint32_t i = 0; i < sizeof(g_w);  i++) { g_w[i]  = rnd8(); }
  for (uint32_t c = 0; c < OUT_CH; c++)
  {
    g_bias[c]  = (int32_t)rnd8() * 64;
    g_mult[c]  = 0x40000000 + (int32_t)c * 0x01000000;   /* ~0.50 .. ~0.73 */
    g_shift[c] = -9 - (int32_t)(c & 1U);                  /* /512 or /1024  */
  }
}

/* Plain C reference: four nested loops, one MAC at a time */
static void Conv1D_Reference(void)
{
  for (int32_t x = 0; x < SIG_LEN; x++)
  {
    for (int32_t oc = 0; oc < OUT_CH; oc++)
    {
      int32_t acc = g_bias[oc];
      for (int32_t k = 0; k < KERNEL; k++)
      {
        int32_t ix = x + k - PAD;
        if ((ix < 0) || (ix >= SIG_LEN)) { continue; }      /* zero padding */
        const int8_t *pin = &g_in[ix * IN_CH];
        const int8_t *pw  = &g_w[(oc * KERNEL + k) * IN_CH];
        for (int32_t ic = 0; ic < IN_CH; ic++)
        {
          acc += ((int32_t)pin[ic] - IN_ZP) * (int32_t)pw[ic];
        }
      }
      acc  = arm_nn_requantize(acc, g_mult[oc], g_shift[oc]);
      acc += OUT_ZP;
      if (acc >  127) { acc =  127; }
      if (acc < -128) { acc = -128; }
      g_out_ref[x * OUT_CH + oc] = (int8_t)acc;
    }
  }
}

/* Same layer through CMSIS-NN (Helium/MVE kernels on Cortex-M55) */
static int32_t Conv1D_CmsisNN(void)
{
  cmsis_nn_context ctx = { .buf = g_scratch, .size = sizeof(g_scratch) };
  cmsis_nn_conv_params cp = {0};
  cmsis_nn_per_channel_quant_params qp = { .multiplier = g_mult, .shift = g_shift };
  cmsis_nn_dims in_d  = { .n = 1, .h = 1, .w = SIG_LEN, .c = IN_CH  };
  cmsis_nn_dims f_d   = { .n = OUT_CH, .h = 1, .w = KERNEL, .c = IN_CH };
  cmsis_nn_dims b_d   = { .n = 1, .h = 1, .w = 1, .c = OUT_CH };
  cmsis_nn_dims out_d = { .n = 1, .h = 1, .w = SIG_LEN, .c = OUT_CH };

  cp.input_offset   = -IN_ZP;
  cp.output_offset  = OUT_ZP;
  cp.stride.h = 1;  cp.stride.w = 1;
  cp.padding.h = 0; cp.padding.w = PAD;
  cp.dilation.h = 1; cp.dilation.w = 1;
  cp.activation.min = -128;
  cp.activation.max = 127;

  return (int32_t)arm_convolve_s8(&ctx, &cp, &qp, &in_d, g_in, &f_d, g_w,
                                  &b_d, g_bias, NULL, &out_d, g_out_nn);
}

static int32_t Scratch_Needed(void)
{
  cmsis_nn_dims in_d = { .n = 1, .h = 1, .w = SIG_LEN, .c = IN_CH };
  cmsis_nn_dims f_d  = { .n = OUT_CH, .h = 1, .w = KERNEL, .c = IN_CH };
  return arm_convolve_s8_get_buffer_size(&in_d, &f_d);
}

static uint32_t Count_Mismatch(void)
{
  uint32_t bad = 0U;
  for (uint32_t i = 0; i < sizeof(g_out_ref); i++)
  {
    if (g_out_ref[i] != g_out_nn[i]) { bad++; }
  }
  return bad;
}

/* ------------------------------ Benchmarking ------------------------------ */
static void DWT_Init(void)
{
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t g_status;   /* last CMSIS-NN return code */

static void Run_Reference(void) { Conv1D_Reference(); }
static void Run_CmsisNN(void)   { g_status = (uint32_t)Conv1D_CmsisNN(); }

/* Fastest of BENCH_RUNS runs, interrupts off so nothing else interferes */
static uint32_t Measure(void (*fn)(void))
{
  uint32_t best = 0xFFFFFFFFU;
  for (uint32_t r = 0U; r < BENCH_RUNS; r++)
  {
    __disable_irq();
    uint32_t t0 = DWT->CYCCNT;
    fn();
    uint32_t t1 = DWT->CYCCNT;
    __enable_irq();
    if ((t1 - t0) < best) { best = t1 - t0; }
  }
  return best;
}

static void Print_Line(const char *label, uint32_t cyc)
{
  /* MACs per cycle x100, time in microseconds at SystemCoreClock */
  uint32_t mpc100 = (uint32_t)(((uint64_t)LAYER_MACS * 100U) / cyc);
  uint32_t us     = (uint32_t)(((uint64_t)cyc * 1000000U) / SystemCoreClock);
  printf("  %-26s %9lu cycles  %7lu us  %lu.%02lu MAC/cycle\r\n", label,
         (unsigned long)cyc, (unsigned long)us,
         (unsigned long)(mpc100 / 100U), (unsigned long)(mpc100 % 100U));
}

static void Ratio(const char *label, uint32_t slow, uint32_t fast)
{
  uint32_t x10 = (uint32_t)(((uint64_t)slow * 10U) / fast);
  printf("  %-40s %lu.%lux\r\n", label, (unsigned long)(x10 / 10U), (unsigned long)(x10 % 10U));
}

static void Run_Day6(void)
{
  uint32_t ref_off, nn_off, ref_on, nn_on, bad;
  int32_t  need = Scratch_Needed();

  printf("Layer: Conv1D int8, signal %d x %d ch -> %d ch, kernel %d, %lu MACs\r\n",
         SIG_LEN, IN_CH, OUT_CH, KERNEL, (unsigned long)LAYER_MACS);
  printf("Helium (MVE) in this build: %s\r\n",
#if defined(ARM_MATH_MVEI)
         "YES"
#else
         "NO"
#endif
  );
  printf("Compiler optimisation: %s\r\n",
#if defined(__OPTIMIZE__)
         "ON (-O1/-O2/-O3/-Os)"
#else
         "OFF (-O0)"
#endif
  );
  printf("CMSIS-NN scratch buffer: needs %ld bytes, have %u\r\n",
         (long)need, (unsigned)sizeof(g_scratch));
  if ((need < 0) || ((uint32_t)need > sizeof(g_scratch))) { Error_Handler(); }

  Layer_InitData();

  printf("\r\nCaches: I %s, D %s\r\n",
         (SCB->CCR & SCB_CCR_IC_Msk) ? "ON" : "OFF",
         (SCB->CCR & SCB_CCR_DC_Msk) ? "ON" : "OFF");
  ref_off = Measure(Run_Reference);
  nn_off  = Measure(Run_CmsisNN);
  bad     = Count_Mismatch();
  Print_Line("plain C reference", ref_off);
  Print_Line("CMSIS-NN (Helium)", nn_off);
  printf("  CMSIS-NN status %lu, outputs differing: %lu of %u\r\n",
         (unsigned long)g_status, (unsigned long)bad, (unsigned)sizeof(g_out_ref));

  SCB_EnableICache();
  SCB_EnableDCache();
  printf("\r\nCaches: I %s, D %s\r\n",
         (SCB->CCR & SCB_CCR_IC_Msk) ? "ON" : "OFF",
         (SCB->CCR & SCB_CCR_DC_Msk) ? "ON" : "OFF");
  ref_on = Measure(Run_Reference);
  nn_on  = Measure(Run_CmsisNN);
  bad    = Count_Mismatch();
  Print_Line("plain C reference", ref_on);
  Print_Line("CMSIS-NN (Helium)", nn_on);
  printf("  CMSIS-NN status %lu, outputs differing: %lu of %u\r\n",
         (unsigned long)g_status, (unsigned long)bad, (unsigned)sizeof(g_out_ref));

  printf("\r\nSpeed-ups:\r\n");
  Ratio("CMSIS-NN vs plain C, caches off", ref_off, nn_off);
  Ratio("CMSIS-NN vs plain C, caches on",  ref_on,  nn_on);
  Ratio("caches on vs off, plain C",       ref_off, ref_on);
  Ratio("caches on vs off, CMSIS-NN",      nn_off,  nn_on);
  Ratio("best (NN + caches) vs worst",     ref_off, nn_on);

  printf("\r\nFirst outputs (ref / nn): ");
  for (uint32_t i = 0U; i < 6U; i++) { printf("%d/%d ", g_out_ref[i], g_out_nn[i]); }
  printf("\r\n");
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

  printf("\r\n=== NUCLEO-N657X0-Q Day 6: TinyML on Cortex-M55 ===\r\n");
  printf("SystemCoreClock = %lu Hz\r\n", (unsigned long)SystemCoreClock);
  Run_Day6();
  printf("\r\nDone. Red LED blinks = finished.\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
    HAL_Delay(500);
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
