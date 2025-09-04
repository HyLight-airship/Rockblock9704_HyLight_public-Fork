/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * This is the main application file for the Rockblock 9704 modem port.
  * It handles hardware initialization, library setup, and the main
  * asynchronous processing loop.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "rockblock_9704.h"
#include "serial_stm32.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h> // Required for _write() syscall function
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;

/* USER CODE BEGIN PV */
// --- Application state variables ---
static volatile int messages_sent_count = 0;
static volatile int messages_received_count = 0;
static volatile int messages_acknowledged_count = 0;
static volatile bool new_message_received = false;
static int current_signal_bars = -1;

// --- GPIO table with your specific pin assignments ---
// This uses the macros defined in main.h, which are generated from your .ioc file.
const rbGpioTable_t customGpioTable =
{
    { POWER_EN_GPIO_Port,    POWER_EN_Pin    }, // PB9
    { IRIDIUM_EN_GPIO_Port,  IRIDIUM_EN_Pin  }, // PB8
    { IRIDIUM_BTD_GPIO_Port, IRIDIUM_BTD_Pin }  // PB4
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_GPIO_USART2_Init(void);
static void MX_USART2_UART_Init(uint32_t baud);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief Retargets the C library printf function to the USART2 peripheral (VCP).
  * This is the standard syscall for ARM-GCC.
  * @param file The file descriptor.
  * @param ptr A pointer to the data to send.
  * @param len The number of bytes to send.
  * @return The number of bytes sent, or -1 on error.
  */
/* Bring up the board "COM" (USB VCP) so printf uses the on-board USB port */
void BspCOM_Init(void)
{
    MX_GPIO_USART2_Init();
    MX_USART2_UART_Init(115200);

    /* Optional: make stdio unbuffered so logs flush immediately */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* Retarget printf/puts to USART2 (USB VCP) */
int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO) {
        (void)HAL_UART_Transmit(&huart2, (uint8_t*)ptr, (uint16_t)len, HAL_MAX_DELAY);
        return len;
    }
    return -1;
}

// --- Rockblock Library Callback Implementations ---

void onMessageProvisioning(const jsprMessageProvisioning_t *messageProvisioning)
{
    if(messageProvisioning->provisioningSet == true)
    {
        printf("Modem is provisioned for %d topics:\r\n", messageProvisioning->topicCount);
        for(int i = 0; i < messageProvisioning->topicCount; i++)
        {
            printf("  - Topic Name: %s, ID: %d\r\n",
            messageProvisioning->provisioning[i].topicName, (int)messageProvisioning->provisioning[i].topicId);
        }
    }
}

void onMoComplete(const uint16_t id, const rbMsgStatus_t status)
{
    if(status == RB_MSG_STATUS_OK)
    {
        messages_sent_count++;
        printf("✅ MO message #%u sent successfully! (Total sent: %d)\r\n", id, messages_sent_count);
    }
    else
    {
        printf("❌ MO message #%u failed to send.\r\n", id);
    }
}

void onMtComplete(const uint16_t id, const rbMsgStatus_t status)
{
    if(status == RB_MSG_STATUS_OK)
    {
        messages_received_count++;
        new_message_received = true; // Signal the main loop to process the message
        printf("✅ MT message #%u received! (Total received: %d)\r\n", id, messages_received_count);
    }
    else
    {
         printf("❌ MT message #%u failed to receive.\r\n", id);
    }
}

void onConstellationState(const jsprConstellationState_t *state)
{
    if(state->signalBars != current_signal_bars)
    {
        printf("🛰️  Signal strength changed to %d/5 bars.\r\n", state->signalBars);
        current_signal_bars = state->signalBars;
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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  BspCOM_Init();               /* On-board USB console ready (ST-LINK VCP) */
  /* USER CODE BEGIN 2 */

  // Make printf unbuffered so logs appear immediately
  setvbuf(stdout, NULL, _IONBF, 0);

  printf("\r\n--- Rockblock 9704 STM32 Port --- \r\n");
  printf("Initializing modem with GPIO control...\r\n");

  // 1. Register the callback functions with the library
  rbCallbacks_t myCallbacks = {
      .messageProvisioning = onMessageProvisioning,
      .moMessageComplete = onMoComplete,
      .mtMessageComplete = onMtComplete,
      .constellationState = onConstellationState
  };
  rbRegisterCallbacks(&myCallbacks);

  // 2. Set the STM32 serial context to use USART1
  setContextStm32(&huart1);

  // 3. Initialize the modem using GPIO. Timeout of 60 seconds.
  if(!rbBeginGpio("USART1", &customGpioTable, 60))
  {
      printf("❌ Modem initialization failed. Please check wiring and power. Halting.\r\n");
      Error_Handler();
  }
  printf("✅ Modem booted and serial session started successfully!\r\n");

  char *imei = rbGetImei();
  if (imei != NULL && strlen(imei) > 0)
  {
      printf("Modem IMEI: %s\r\n", imei);
  }
  else
  {
      printf("Warning: Could not retrieve IMEI.\r\n");
  }


  // 4. Queue five messages to be sent asynchronously
  printf("Queuing 5 test messages...\r\n");
  int messages_queued = 0;
  if(rbSendMessageAsync(RAW_TOPIC, "Test Message 1", strlen("Test Message 1"))) messages_queued++;
  if(rbSendMessageAsync(RAW_TOPIC, "Test Message 2", strlen("Test Message 2"))) messages_queued++;
  if(rbSendMessageAsync(RAW_TOPIC, "Test Message 3", strlen("Test Message 3"))) messages_queued++;
  if(rbSendMessageAsync(RAW_TOPIC, "Test Message 4", strlen("Test Message 4"))) messages_queued++;
  if(rbSendMessageAsync(RAW_TOPIC, "Test Message 5", strlen("Test Message 5"))) messages_queued++;
  printf("%d messages queued for transmission.\r\n", messages_queued);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    // 5. Poll the library frequently. This handles all serial communication.
    rbPoll();

    // 6. Check if the MT callback signaled a new message
    if (new_message_received)
    {
        new_message_received = false; // Reset the flag
        char *mt_buffer = NULL;
        size_t mt_length = rbReceiveMessageAsync(&mt_buffer);

        if (mt_length > 0 && mt_buffer != NULL)
        {
            printf("Main loop processed MT message: '");
            // Print as a string, ensuring it's null-terminated for printf
            char temp_buf[mt_length + 1];
            memcpy(temp_buf, mt_buffer, mt_length);
            temp_buf[mt_length] = '\0';
            printf("%s", temp_buf);
            printf("' (Length: %u)\r\n", (unsigned int)mt_length);

            // 7. Acknowledge the message to remove it from the queue
            if(rbAcknowledgeReceiveHeadAsync())
            {
                messages_acknowledged_count++;
                printf("Message acknowledged. (Total acknowledged: %d)\r\n", messages_acknowledged_count);
            }
        }
    }

    // Break the loop for this example after 5 messages have been acknowledged
    if (messages_acknowledged_count >= 5)
    {
        break;
    }

    HAL_Delay(10); // A small delay is good practice
  }
  /* USER CODE END 3 */

  printf("\r\n--- Example Finished --- \r\n");
  printf("Shutting down modem...\r\n");
  if(rbEndGpio(&customGpioTable))
  {
      printf("✅ Modem powered down and connection closed successfully.\r\n");
  }
  else
  {
      printf("❌ Failed to shut down modem.\r\n");
  }

  // Loop forever after shutdown
  while(1);
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
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
  huart1.Init.BaudRate = 230400;
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

/* USART2 on PA2 (TX) / PA3 (RX) for ST-LINK VCP (same USB used to flash) */
static void MX_GPIO_USART2_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3; /* PA2 TX, PA3 RX */
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void MX_USART2_UART_Init(uint32_t baud)
{
    __HAL_RCC_USART2_CLK_ENABLE();

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = baud;                 /* 115200 typical */
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief DMA Initialization Function
  * @param None
  * @retval None
  */
static void MX_DMA_Init(void)
{

  /* USER CODE BEGIN DMA_Init 0 */

  /* USER CODE END DMA_Init 0 */

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

  /* USER CODE BEGIN DMA_Init 1 */

  /* USER CODE END DMA_Init 1 */

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, IRIDIUM_EN_Pin|POWER_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : BUTTON_USER_Pin */
  GPIO_InitStruct.Pin = BUTTON_USER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BUTTON_USER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : IRIDIUM_BTD_Pin */
  GPIO_InitStruct.Pin = IRIDIUM_BTD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(IRIDIUM_BTD_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : IRIDIUM_EN_Pin POWER_EN_Pin */
  GPIO_InitStruct.Pin = IRIDIUM_EN_Pin|POWER_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* ===== Error handler(s) ===== */

void Error_Handler(void)
{
    /* Blink PB8 quickly to indicate a fault (no HAL_Delay). */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = GPIO_PIN_8;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_8);
        for (volatile uint32_t i = 0; i < 500000; ++i) { __NOP(); }
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
    Error_Handler();
}
#endif
