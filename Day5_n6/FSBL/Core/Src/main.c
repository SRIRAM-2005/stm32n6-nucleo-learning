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
#include "string.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ====================== Day 5: Ethernet MAC + PHY (raw) =====================
 * 1 = MDIO: find the PHY, dump its registers, watch link up/down live
 * 2 = TX : send a raw Ethernet frame every second (see it in Wireshark)
 * 3 = RX : promiscuous mode, print every frame the MAC receives
 * Everything runs from RAM in development boot. Nothing permanent is written.
 * D-cache stays OFF on purpose: the Ethernet DMA reads/writes RAM directly.
 * ========================================================================== */
#define DAY5_EXERCISE    3

#define FRAME_ETHERTYPE  0x88B5U  /* IEEE 802 "local experimental" EtherType 1 */
#define ETH_BUF_SIZE     1536U    /* must match RxBuffLen set in CubeMX        */
#define RX_POOL_COUNT    12U      /* >= 2 DMA channels x ETH_RX_DESC_CNT + spare */

/* IEEE 802.3 Clause 22 standard PHY registers (same on every PHY) */
#define PHY_BMCR    0U    /* basic control                */
#define PHY_BMSR    1U    /* basic status                 */
#define PHY_ID1     2U    /* PHY identifier 1             */
#define PHY_ID2     3U    /* PHY identifier 2             */
#define PHY_ANAR    4U    /* auto-neg advertisement (ours) */
#define PHY_ANLPAR  5U    /* auto-neg link partner ability */
#define PHY_ANER    6U    /* auto-neg expansion           */

#define BMSR_LINK      (1U << 2)
#define BMSR_AN_DONE   (1U << 5)
#define BMCR_AN_EN     (1U << 12)
#define AN_100FD       (1U << 8)
#define AN_100HD       (1U << 7)
#define AN_10FD        (1U << 6)
#define AN_10HD        (1U << 5)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
#if defined ( __ICCARM__ ) /*!< IAR Compiler */
#pragma location=0x34100000
ETH_DMADescTypeDef  DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
#pragma location=0x341000C0
ETH_DMADescTypeDef  DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __CC_ARM )  /* MDK ARM Compiler */

__attribute__((at(0x34100000))) ETH_DMADescTypeDef  DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
__attribute__((at(0x341000C0))) ETH_DMADescTypeDef  DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __GNUC__ ) /* GNU Compiler */

ETH_DMADescTypeDef DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT] __attribute__((section(".RxDecripSection"))); /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT] __attribute__((section(".TxDecripSection")));   /* Ethernet Tx DMA Descriptors */
#endif

ETH_TxPacketConfig TxConfig;

ETH_HandleTypeDef heth1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static uint32_t g_phy_addr = 0xFFFFFFFFU;          /* found by MDIO scan */

/* RX buffer pool handed to the ETH DMA through the HAL callbacks */
static uint8_t           g_rx_pool[RX_POOL_COUNT][ETH_BUF_SIZE] __attribute__((aligned(32)));
static volatile uint8_t  g_rx_used[RX_POOL_COUNT];
static uint32_t          g_rx_len;

static uint8_t           g_tx_buf[ETH_BUF_SIZE] __attribute__((aligned(32)));

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ETH1_Init(void);
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

/* ------------------- RIF: let the ETH DMA reach secure RAM ----------------
 * The FSBL runs in the secure world and its RAM is secure. The Ethernet DMA
 * is a separate bus master, so it must be marked secure + privileged too,
 * otherwise its RAM accesses are blocked. Same settings as ST's NUCLEO-N657X0-Q
 * Ethernet examples. These are ordinary registers: they reset at power-off.  */
static void Eth_RifConfig(void)
{
  uint32_t v;

  __HAL_RCC_RIFSC_CLK_ENABLE();

  /* Master side (RIMC): ETH1 = master index 6, CID1, secure, privileged */
  v  = RIFSC->RIMC_ATTRx[6];
  v &= ~(RIFSC_RIMC_ATTRx_MCID_Msk | (3UL << RIFSC_RIMC_ATTRx_MSEC_Pos));
  v |= (1UL << RIFSC_RIMC_ATTRx_MCID_Pos) | (3UL << RIFSC_RIMC_ATTRx_MSEC_Pos);
  RIFSC->RIMC_ATTRx[6] = v;

  /* Slave side (RISC): ETH1 registers = register 1, bit 28: secure, privileged */
  RIFSC->RISC_SECCFGRx[1]  |= (1UL << 28);
  RIFSC->RISC_PRIVCFGRx[1] |= (1UL << 28);
}

/* --------------------------- MDIO / PHY helpers --------------------------- */
static uint32_t Phy_Read(uint32_t reg)
{
  uint32_t val = 0U;
  if (HAL_ETH_ReadPHYRegister(&heth1, g_phy_addr, reg, &val) != HAL_OK)
  {
    return 0xFFFFFFFFU;
  }
  return val & 0xFFFFU;
}

/* Try all 32 MDIO addresses; a PHY answers with an ID that isn't 0 or FFFF */
static uint32_t Phy_Scan(void)
{
  for (uint32_t addr = 0U; addr < 32U; addr++)
  {
    uint32_t id1 = 0U;
    if ((HAL_ETH_ReadPHYRegister(&heth1, addr, PHY_ID1, &id1) == HAL_OK) &&
        (id1 != 0x0000U) && (id1 != 0xFFFFU))
    {
      return addr;
    }
  }
  return 0xFFFFFFFFU;
}

/* BMSR's link bit is "latched low": the first read reports any drop since
 * the last read, so read twice to get the current state.                   */
static uint32_t Phy_ReadBmsr(void)
{
  (void)Phy_Read(PHY_BMSR);
  return Phy_Read(PHY_BMSR);
}

/* Highest mode both ends advertised = what auto-negotiation chose */
static void Phy_Resolve(uint32_t *speed, uint32_t *duplex, const char **text)
{
  uint32_t common = Phy_Read(PHY_ANAR) & Phy_Read(PHY_ANLPAR);

  if      (common & AN_100FD) { *speed = ETH_SPEED_100M; *duplex = ETH_FULLDUPLEX_MODE; *text = "100 Mbit/s full duplex"; }
  else if (common & AN_100HD) { *speed = ETH_SPEED_100M; *duplex = ETH_HALFDUPLEX_MODE; *text = "100 Mbit/s half duplex"; }
  else if (common & AN_10FD)  { *speed = ETH_SPEED_10M;  *duplex = ETH_FULLDUPLEX_MODE; *text = "10 Mbit/s full duplex"; }
  else                        { *speed = ETH_SPEED_10M;  *duplex = ETH_HALFDUPLEX_MODE; *text = "10 Mbit/s half duplex"; }
}

static void Phy_Dump(void)
{
  uint32_t bmcr = Phy_Read(PHY_BMCR), bmsr = Phy_ReadBmsr();
  uint32_t id1 = Phy_Read(PHY_ID1),   id2 = Phy_Read(PHY_ID2);

  printf("PHY at MDIO address %lu\r\n", (unsigned long)g_phy_addr);
  printf("  reg0 BMCR   = 0x%04lX  auto-neg %s\r\n", (unsigned long)bmcr,
         (bmcr & BMCR_AN_EN) ? "enabled" : "disabled");
  printf("  reg1 BMSR   = 0x%04lX  link %s, auto-neg %s\r\n", (unsigned long)bmsr,
         (bmsr & BMSR_LINK) ? "UP" : "down", (bmsr & BMSR_AN_DONE) ? "complete" : "not complete");
  printf("  reg2 ID1    = 0x%04lX\r\n", (unsigned long)id1);
  printf("  reg3 ID2    = 0x%04lX  (model 0x%02lX, revision %lu)\r\n", (unsigned long)id2,
         (unsigned long)((id2 >> 4) & 0x3FU), (unsigned long)(id2 & 0xFU));
  printf("  reg4 ANAR   = 0x%04lX  we advertise\r\n", (unsigned long)Phy_Read(PHY_ANAR));
  printf("  reg5 ANLPAR = 0x%04lX  partner advertises\r\n", (unsigned long)Phy_Read(PHY_ANLPAR));
  printf("  reg6 ANER   = 0x%04lX\r\n", (unsigned long)Phy_Read(PHY_ANER));
}

static void Print_Mac(const uint8_t *m)
{
  printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* Wait for the cable/link, then tell the MAC the negotiated speed/duplex.
 * The PHY and MAC must agree, or frames are corrupted.                      */
static void Eth_WaitLinkAndStart(void)
{
  ETH_MACConfigTypeDef mac;
  uint32_t speed, duplex;
  const char *text;
  uint32_t waited = 0U;

  printf("Waiting for link (plug the cable into the laptop)...\r\n");
  while ((Phy_ReadBmsr() & (BMSR_LINK | BMSR_AN_DONE)) != (BMSR_LINK | BMSR_AN_DONE))
  {
    HAL_Delay(100);
    if ((++waited % 50U) == 0U) { printf("  still waiting, %lu s\r\n", (unsigned long)(waited / 10U)); }
  }
  Phy_Resolve(&speed, &duplex, &text);
  printf("Link UP: %s\r\n", text);

  if (HAL_ETH_GetMACConfig(&heth1, &mac) != HAL_OK) { Error_Handler(); }
  mac.Speed      = speed;
  mac.DuplexMode = duplex;
  if (HAL_ETH_SetMACConfig(&heth1, &mac) != HAL_OK) { Error_Handler(); }
  if (HAL_ETH_Start(&heth1) != HAL_OK)              { Error_Handler(); }
  printf("MAC + DMA started\r\n");
}

/* --------------------- HAL ETH RX buffer callbacks ------------------------ */
/* HAL asks for an empty buffer to give to an RX DMA descriptor */
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
  *buff = NULL;
  for (uint32_t i = 0U; i < RX_POOL_COUNT; i++)
  {
    if (g_rx_used[i] == 0U)
    {
      g_rx_used[i] = 1U;
      *buff = g_rx_pool[i];
      return;
    }
  }
}

/* HAL hands back a filled buffer. RxBuffLen (1536) holds any normal frame,
 * so every frame arrives in exactly one buffer.                             */
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
  if (*pStart == NULL)
  {
    *pStart  = buff;
    g_rx_len = 0U;
  }
  *pEnd     = buff;
  g_rx_len += Length;
}

static void Rx_Free(const uint8_t *buff)
{
  uint32_t i = (uint32_t)(buff - &g_rx_pool[0][0]) / ETH_BUF_SIZE;
  if (i < RX_POOL_COUNT) { g_rx_used[i] = 0U; }
}

static const char *EtherType_Name(uint32_t t)
{
  switch (t)
  {
    case 0x0800U: return "IPv4";
    case 0x0806U: return "ARP";
    case 0x86DDU: return "IPv6";
    case 0x88CCU: return "LLDP";
    case 0x8100U: return "VLAN";
    case FRAME_ETHERTYPE: return "experimental (ours)";
    default: return (t <= 1500U) ? "802.3 length field" : "other";
  }
}

/* ============================== Exercise 1 =============================== */
__attribute__((unused)) static void Run_Exercise1(void)
{
  uint32_t last = 0xFFFFFFFFU;

  Phy_Dump();
  printf("Watching the link. Plug / unplug the cable:\r\n");
  while (1)
  {
    uint32_t bmsr = Phy_ReadBmsr();
    uint32_t up = ((bmsr & (BMSR_LINK | BMSR_AN_DONE)) == (BMSR_LINK | BMSR_AN_DONE)) ? 1U : 0U;
    if (up != last)
    {
      last = up;
      if (up)
      {
        uint32_t s, d; const char *t;
        Phy_Resolve(&s, &d, &t);
        printf("[%6lu ms] LINK UP   %s  (ANLPAR=0x%04lX)\r\n",
               (unsigned long)HAL_GetTick(), t, (unsigned long)Phy_Read(PHY_ANLPAR));
      }
      else
      {
        printf("[%6lu ms] LINK DOWN\r\n", (unsigned long)HAL_GetTick());
      }
      HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, up ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
    HAL_Delay(200);
  }
}

/* ============================== Exercise 2 =============================== */
static uint32_t Build_Frame(uint32_t n)
{
  uint8_t *f = g_tx_buf;
  int len;

  memset(f, 0xFF, 6);                                /* dst: broadcast        */
  memcpy(&f[6], heth1.Init.MACAddr, 6);              /* src: our MAC          */
  f[12] = (uint8_t)(FRAME_ETHERTYPE >> 8);           /* EtherType, big-endian */
  f[13] = (uint8_t)(FRAME_ETHERTYPE & 0xFFU);
  len = snprintf((char *)&f[14], ETH_BUF_SIZE - 14U,
                 "Hello from STM32N6 frame #%lu, uptime %lu ms",
                 (unsigned long)n, (unsigned long)HAL_GetTick());
  /* the MAC pads short frames to 60 bytes and appends the 4-byte CRC */
  return 14U + (uint32_t)len;
}

__attribute__((unused)) static void Run_Exercise2(void)
{
  ETH_BufferTypeDef         txbuf;
  ETH_TxPacketConfigTypeDef tx;
  uint32_t n = 0U, ok = 0U;

  printf("Our MAC: "); Print_Mac(heth1.Init.MACAddr); printf("\r\n");
  Eth_WaitLinkAndStart();

  memset(&tx, 0, sizeof(tx));
  tx.TxDMACh      = ETH_DMA_CH0_IDX;
  tx.Attributes   = ETH_TX_PACKETS_FEATURES_CRCPAD;
  tx.CRCPadCtrl   = ETH_CRC_PAD_INSERT;
  tx.ChecksumCtrl = ETH_CHECKSUM_DISABLE;
  tx.SrcAddrCtrl  = ETH_SRC_ADDR_CONTROL_DISABLE;
  tx.TxBuffer     = &txbuf;

  printf("Sending a broadcast frame every second, EtherType 0x%04X.\r\n", FRAME_ETHERTYPE);
  printf("Wireshark display filter:  eth.type == 0x%04x\r\n", FRAME_ETHERTYPE);
  while (1)
  {
    uint32_t len = Build_Frame(++n);
    txbuf.buffer = g_tx_buf;
    txbuf.len    = len;
    txbuf.next   = NULL;
    tx.Length    = len;
    tx.pData     = g_tx_buf;

    if (HAL_ETH_Transmit(&heth1, &tx, 100U) == HAL_OK) { ok++; }
    (void)HAL_ETH_ReleaseTxPacket(&heth1);   /* give the descriptor back */

    printf("frame %lu sent (%lu bytes before pad/CRC), ok=%lu, err=0x%lX\r\n",
           (unsigned long)n, (unsigned long)len, (unsigned long)ok,
           (unsigned long)heth1.ErrorCode);
    if (n == 1U)
    {
      printf("  bytes: ");
      for (uint32_t i = 0U; i < 24U; i++) { printf("%02X ", g_tx_buf[i]); }
      printf("...\r\n");
    }
    HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
    HAL_Delay(1000);
  }
}

/* ============================== Exercise 3 =============================== */
__attribute__((unused)) static void Run_Exercise3(void)
{
  ETH_MACFilterConfigTypeDef filt;
  uint32_t count = 0U, last_report = HAL_GetTick();

  printf("Our MAC: "); Print_Mac(heth1.Init.MACAddr); printf("\r\n");
  Eth_WaitLinkAndStart();

  /* Promiscuous: accept every frame, not only ours / broadcast */
  if (HAL_ETH_GetMACFilterConfig(&heth1, &filt) != HAL_OK) { Error_Handler(); }
  filt.PromiscuousMode = ENABLE;
  if (HAL_ETH_SetMACFilterConfig(&heth1, &filt) != HAL_OK) { Error_Handler(); }
  printf("Promiscuous mode ON. Printing every received frame:\r\n");

  while (1)
  {
    void *pkt = NULL;
    if ((HAL_ETH_ReadData(&heth1, &pkt) == HAL_OK) && (pkt != NULL))
    {
      const uint8_t *f = (const uint8_t *)pkt;
      uint32_t type = ((uint32_t)f[12] << 8) | f[13];
      count++;
      printf("#%-4lu %4lu B  ", (unsigned long)count, (unsigned long)g_rx_len);
      Print_Mac(&f[6]); printf(" -> "); Print_Mac(&f[0]);
      printf("  type 0x%04lX %s\r\n", (unsigned long)type, EtherType_Name(type));
      Rx_Free(f);
      HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    }
    if ((HAL_GetTick() - last_report) >= 10000U)
    {
      last_report = HAL_GetTick();
      printf("--- %lu frames so far ---\r\n", (unsigned long)count);
    }
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
  Eth_RifConfig();   /* before the ETH DMA ever touches RAM */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_ETH1_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);

  printf("\r\n=== NUCLEO-N657X0-Q Day 5: Ethernet MAC + PHY ===\r\n");
  printf("SystemCoreClock = %lu Hz, exercise %d\r\n",
         (unsigned long)SystemCoreClock, DAY5_EXERCISE);

  g_phy_addr = Phy_Scan();
  if (g_phy_addr == 0xFFFFFFFFU)
  {
    printf("No PHY answered on MDIO (addresses 0-31). Check ETH pins in CubeMX.\r\n");
    Error_Handler();
  }

#if   (DAY5_EXERCISE == 1)
  Run_Exercise1();
#elif (DAY5_EXERCISE == 2)
  Run_Exercise2();
#elif (DAY5_EXERCISE == 3)
  Run_Exercise3();
#else
#error "DAY5_EXERCISE must be 1, 2 or 3"
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
  * @brief ETH1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH1_Init(void)
{

  /* USER CODE BEGIN ETH1_Init 0 */

  /* USER CODE END ETH1_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH1_Init 1 */

  /* USER CODE END ETH1_Init 1 */

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  heth1.Instance = ETH1;
  MACAddr[0] = 0x02;
  MACAddr[1] = 0x00;
  MACAddr[2] = 0x00;
  MACAddr[3] = 0x4E;
  MACAddr[4] = 0x36;
  MACAddr[5] = 0x01;
  heth1.Init.MACAddr = &MACAddr[0];
  heth1.Init.MediaInterface = HAL_ETH_RMII_MODE;
  for (int ch = 0; ch < ETH_DMA_CH_CNT; ch++)
  {
    heth1.Init.TxDesc[ch] = DMATxDscrTab[ch];
    heth1.Init.RxDesc[ch] = DMARxDscrTab[ch];
  }
  heth1.Init.RxBuffLen = 1536;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ETH1_Init 2 */

  /* USER CODE END ETH1_Init 2 */

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
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
