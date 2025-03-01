/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "mbedtls.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdbool.h>
#include "lwip.h"
#include "ppp.h"
#include "libGSM.h"
#include "client_blynk.h"
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
RNG_HandleTypeDef hrng;

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart6;
DMA_HandleTypeDef hdma_usart6_rx;

osThreadId defaultTaskHandle;
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USART6_UART_Init(void);
static void MX_RNG_Init(void);
void StartDefaultTask(void const * argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Create a mutex handle
osMutexId uartMutexHandle;
osMutexDef(uartMutex);  // Mutex definition (can be placed globally or in init)

int _write(int file, char* data, int len)
{
   if (uartMutexHandle != NULL) {
      // Lock the mutex before accessing the UART
      osMutexWait(uartMutexHandle, osWaitForever);
   }

   // Transmit data via UART
   HAL_UART_Transmit(&huart3, (uint8_t*)data, len, 1000);

   if (uartMutexHandle != NULL) {
      // Release the mutex after the transmission is complete
      osMutexRelease(uartMutexHandle);
   }

   return len;  // Return the number of bytes written
}

// Initialize the mutex (call this in your system initialization code)
void UART_Mutex_Init(void) {
   uartMutexHandle = osMutexCreate(osMutex(uartMutex));
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
   UART_Mutex_Init();
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
  MX_USART3_UART_Init();
  MX_USART6_UART_Init();
  MX_RNG_Init();
  /* Call PreOsInit function */
  MX_MBEDTLS_Init();
  /* USER CODE BEGIN 2 */
   MX_LWIP_Init();
   printf("Low Level init complete\r\n");
  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
   /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
   /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
   /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
   /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 1024);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
   /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

   while (1)
   {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
   }
  /* USER CODE END 3 */
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 72;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 115200;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void my_debug(void* ctx, int level, const char* file, int line, const char* str) {
   ((void)ctx);
   printf("%s:%d: %s", file, line, str);
}

#include "lwip/sockets.h"
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

extern const char* CA_CERT;

void https_reg_test() {
   int ret;
   mbedtls_net_context server_fd;
   mbedtls_ssl_context ssl;
   mbedtls_ssl_config conf;
   mbedtls_x509_crt cacert;
   mbedtls_ctr_drbg_context ctr_drbg;
   mbedtls_entropy_context entropy;

   const char* pers = "https_client";
   const char* server = "blynk.cloud";
   const char* port = "443";

   char request[] =
      "GET / HTTP/1.1\r\n"
      "Host: blynk.cloud\r\n"
      "User-Agent: curl/7.88.1\r\n"
      "Accept: */*\r\n"
      "Connection: close\r\n\r\n";
   char buffer[256];

   // Initialize mbedTLS structures
  //  mbedtls_net_init(&server_fd);
   mbedtls_ssl_init(&ssl);
   mbedtls_ssl_config_init(&conf);
   mbedtls_x509_crt_init(&cacert);
   mbedtls_ctr_drbg_init(&ctr_drbg);
   mbedtls_entropy_init(&entropy);

   mbedtls_ssl_conf_dbg(&conf, my_debug, NULL);
   mbedtls_debug_set_threshold(1);

   // Seed the random number generator
   ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
   if (ret != 0) {
      printf("mbedtls_ctr_drbg_seed failed: -0x%x\n", -ret);
      return;
   }

   // Load the trusted CA certificate
   ret = mbedtls_x509_crt_parse(&cacert, (const unsigned char*)CA_CERT, strlen(CA_CERT) + 1);
   if (ret < 0) {
      printf("mbedtls_x509_crt_parse failed: -0x%x\n", -ret);
      return;
   }

   // Connect to the server
   ret = mbedtls_net_connect(&server_fd, server, port, MBEDTLS_NET_PROTO_TCP);
   if (ret != 0) {
      printf("mbedtls_net_connect failed: -0x%x\n", -ret);
      return;
   }

   // SSL/TLS configuration
   mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);

   // mbedtls_ssl_set_timer_cb(&ssl, &timer_ctx, timer_set, timer_get);

   mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
   mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
   mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
   mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3); // TLS 1.2
   mbedtls_ssl_setup(&ssl, &conf);
   mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);
   mbedtls_ssl_set_hostname(&ssl, server);
   mbedtls_ssl_conf_max_frag_len(&conf, MBEDTLS_SSL_MAX_FRAG_LEN_4096);

   // Perform SSL/TLS handshake
   while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
         printf("mbedtls_ssl_handshake failed: -0x%x\n", -ret);
         return;
      }
   }

   // Send HTTP GET request
   printf(request);
   ret = mbedtls_ssl_write(&ssl, (const unsigned char*)request, strlen(request));
   if (ret != strlen(request)) {
      printf("Error sending request: %d\n", ret);
   }

   do {
      ret = mbedtls_ssl_read(&ssl, (unsigned char*)buffer, sizeof(buffer) - 1);
      if (ret > 0) {
         for (int n = 0; n < ret; n++) {
            _write(0, &buffer[n], 1);
         }
      }
   } while (ret > 0);
   printf("\n\nDone!\n");

   // Cleanup
   mbedtls_ssl_close_notify(&ssl);
   mbedtls_net_free(&server_fd);
   mbedtls_x509_crt_free(&cacert);
   mbedtls_ssl_free(&ssl);
   mbedtls_ssl_config_free(&conf);
   mbedtls_ctr_drbg_free(&ctr_drbg);
   mbedtls_entropy_free(&entropy);
}

// #include "lwip/sockets.h"
// #include "lwip/netdb.h"
// #include <string.h>
// #include <stdio.h>

// #define SERVER "example.com"
// #define PORT   80

// void http_reg_test(void) {
//    int sock;
//    struct sockaddr_in server_addr;
//    char request[] = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
//    char response[1024];
//    int bytes_received;

//    sock = socket(AF_INET, SOCK_STREAM, 0);
//    if (sock < 0) {
//       printf("Socket creation failed\n");
//       return;
//    }

//    server_addr.sin_family = AF_INET;
//    server_addr.sin_port = htons(PORT);

//    // Resolve hostname
//    struct hostent* host = gethostbyname(SERVER);
//    if (host == NULL) {
//       printf("DNS resolution failed\n");
//       close(sock);
//       return;
//    }
//    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

//    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
//       printf("Connection failed\n");
//       close(sock);
//       return;
//    }

//    if (send(sock, request, strlen(request), 0) < 0) {
//       printf("Send failed\n");
//       close(sock);
//       return;
//    }

//    while ((bytes_received = recv(sock, response, sizeof(response) - 1, 0)) > 0) {
//       response[bytes_received] = '\0';  // Null-terminate the response
//       printf("%s", response);
//    }

//    close(sock);
//    printf("\nConnection closed\n");
// }
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const* argument) {
   /* USER CODE BEGIN 5 */
   printf("Started StartDefaultTask\r\n");
   mqtt_message_t msg;

   mqtt_init();
   LTE_ppposInit();

  //  mqtt_connect("mqtts://lon1.blynk.cloud:8883");

   for(;;) {
      if(xQueueReceive(mqttQueue, &msg, portMAX_DELAY) == pdPASS) {
        //  printf("Got mqtt message: %d\n", msg.id);
         switch(msg.id) {
         case topic_redirect:
            mqtt_connect((const char*)msg.payload);
            break;
         case topic_disconnect:
         case topic_connect:
            mqtt_connect("mqtts://lon1.blynk.cloud:8883");
            break;
         case topic_ping:
            mqtt_publish_ds("ds/Terminal", "Ping request\n");
            break;
         case topic_reboot:
            mqtt_publish_ds("ds/Terminal", "Reboot request\n");
            NVIC_SystemReset();
            break;
         case topic_power: {
            int on = atoi((const char*)msg.payload);
            printf("Power:%d\n", on);
            char tmp[0xf];
            int status = 1;
            if( on )
              status = 2 + rand()%3;
            sprintf(tmp, "%d", status);
            mqtt_publish_ds("ds/Status", tmp);
         } break;
         case topic_settemperature: {
            int temperature = atoi((const char*)msg.payload);
            printf("Set Temperature:%d\n", temperature);
            char tmp[0xf]; sprintf(tmp, "%d", temperature);
            mqtt_publish_ds("ds/Current Temperature", tmp);
         } break;
         default:
            printf("Unknown topic: %d\n", msg.id);
            break;
         }
         if(msg.payload)
            free(msg.payload);
      }
   }
   /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
   /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
      tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
