/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "mcp2515.h"
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ENCODER_SLOTS 20
#define VREF 3.3f
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
struct can_frame tx_msg;
uint32_t last_send_time = 0;
char uart_buf[150];

/* 1. Biến đo RPM bằng ngắt ngoài PA0 */
volatile uint32_t pulse_count = 0;
uint32_t last_pulse_count = 0;
uint32_t last_time = 0;
float motor_rpm = 0.0f;

/* 2. Biến đo Điện áp ADC chân PA2 */
float v_out_pin = 0.0f;
float v_in_real = 0.0f;

/* 3. Biến đo Khoảng cách siêu âm (Trig PB11, Echo PB10) */
float distance_cm = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
float HCSR04_Read_Distance(void)
{
  uint32_t timeout_counter;
  uint16_t startTime = 0;
  uint16_t travelTime = 0;

  // 1. Kích chân Trig (PB11) lên HIGH trong 10us
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  while (__HAL_TIM_GET_COUNTER(&htim2) < 10);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);

  // 2. Chờ chân Echo (PB10) lên mức HIGH (Có kèm Giới hạn Timeout bằng Timer)
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) == GPIO_PIN_RESET)
  {
    // Nếu quá 3000 us (3ms) mà chưa thấy Echo lên HIGH -> Lỗi cảm biến
    if (__HAL_TIM_GET_COUNTER(&htim2) > 3000)
      return -1.0f;
  }

  // 3. Chốt thời gian bắt đầu và chờ chân Echo xuống LOW
  startTime = __HAL_TIM_GET_COUNTER(&htim2);

  while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10) == GPIO_PIN_SET)
  {
    // Nếu quá tầm đo (ví dụ xa quá 35000 us ~ 6 mét) -> Thoát chống treo máy
    if (__HAL_TIM_GET_COUNTER(&htim2) > 35000)
      return -2.0f;
  }

  // 4. Tính toán thời gian sóng chạy đi và về (us)
  uint16_t endTime = __HAL_TIM_GET_COUNTER(&htim2);
  if (endTime >= startTime)
  {
    travelTime = endTime - startTime;
  }
  else
  {
    travelTime = (65535 - startTime) + endTime; // Xử lý tràn Timer
  }

  // 5. Quy đổi ra cm
  return ((float)travelTime * 0.0343f) / 2.0f;
}
float Get_Median_Filter(float *samples, uint8_t size)
{
  // Sắp xếp mảng theo thứ tự tăng dần (Bubble Sort)
  for (uint8_t i = 0; i < size - 1; i++)
  {
    for (uint8_t j = i + 1; j < size; j++)
    {
      if (samples[i] > samples[j])
      {
        float temp = samples[i];
        samples[i] = samples[j];
        samples[j] = temp;
      }
    }
  }
  // Lấy phần tử ở chính giữa
  return samples[size / 2];
}

/* Biến cấu trúc lọc thông thấp cho ADC */
#define ALPHA 0.15f            // Hệ số mượt (Càng nhỏ càng mượt nhưng đáp ứng chậm lại, từ 0.05 đến 0.2)
float adc_filtered_val = 0.0f; // Biến lưu điện áp sau lọc

float Low_Pass_Filter(float new_sample, float old_filtered)
{
  if (old_filtered == 0.0f)
    return new_sample; // Khởi tạo lượt đầu
  return (ALPHA * new_sample) + ((1.0f - ALPHA) * old_filtered);
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
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  HAL_Delay(50);

  // In dòng chào mừng để test UART hoạt động ngay lập tức
  sprintf(uart_buf, "--- HE THONG TIEN HANH THU THAP VA GUI CAN BUS ---\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t *)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);

  HAL_TIM_Base_Start(&htim2); // Chạy nền Timer 2

  sprintf(uart_buf, "-> Dang khoi tao MCP2515...\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t *)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);

  mcp2515_init(); // Khởi tạo chip CAN
  setNormalMode();

  sprintf(uart_buf, "-> Khoi tao MCP2515 THANH CONG!\r\n");
  HAL_UART_Transmit(&huart1, (uint8_t *)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);

  last_time = HAL_GetTick();
  last_send_time = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    // --- 1. ĐO TỐC ĐỘ MOTOR (ENCODER) THEO CHU KỲ 1 GIÂY ---
    uint32_t current_time = HAL_GetTick();
    if (current_time - last_time >= 1000)
    {
      // Chốt số lượng xung nhận được (có tính toán loại bỏ race condition do ngắt)
      uint32_t current_pulse_count = pulse_count;
      uint32_t dt_ms = current_time - last_time;
      uint32_t pulses = current_pulse_count - last_pulse_count;

      // Công thức tính RPM = (Số xung / Số lỗ encoder) * (60000ms / thời gian lấy mẫu ms)
      motor_rpm = ((float)pulses / (float)ENCODER_SLOTS) * (60000.0f / (float)dt_ms);

      // Cập nhật lại biến lưu trữ thời gian và xung cũ
      last_pulse_count = current_pulse_count;
      last_time = current_time;
    }

    // --- 2. ĐO KHOẢNG CÁCH SIÊU ÂM HC-SR04 ---
#define ULTRASONIC_SAMPLES 5
    float dist_samples[ULTRASONIC_SAMPLES];
    uint8_t valid_count = 0;

    for (uint8_t i = 0; i < ULTRASONIC_SAMPLES; i++)
    {
      float raw_dist = HCSR04_Read_Distance();

      // Chỉ nhận các giá trị đo hợp lệ (không lỗi dây -1, không quá tầm -2)
      if (raw_dist > 0.0f)
      {
        dist_samples[valid_count] = raw_dist;
        valid_count++;
      }
      HAL_Delay(15); // Khoảng nghỉ nhỏ giữa các lần phát siêu âm để tránh nhiễu tiếng vang chồng chéo
    }

    if (valid_count > 0)
    {
      // Lọc bỏ các giá trị nhảy vọt đột biến, giữ lại giá trị chuẩn ở giữa mảng
      float median_dist = Get_Median_Filter(dist_samples, valid_count);

      // Bổ sung bộ lọc mượt cho cảm biến siêu âm: 70% giá trị cũ + 30% giá trị mới
      if (distance_cm == 0.0f)
      {
        distance_cm = median_dist;
      }
      else
      {
        distance_cm = (0.7f * distance_cm) + (0.3f * median_dist);
      }
    }

    // --- 3. ĐO ĐIỆN ÁP TỪ CHÂN ADC (PA2) ---
    uint32_t adc_sum = 0;
    uint8_t adc_samples = 8;
    for (uint8_t i = 0; i < adc_samples; i++)
    {
      HAL_ADC_Start(&hadc1);
      if (HAL_ADC_PollForConversion(&hadc1, 5) == HAL_OK)
      {
        adc_sum += HAL_ADC_GetValue(&hadc1);
      }
      HAL_ADC_Stop(&hadc1);
    }

    float adc_avg = (float)adc_sum / (float)adc_samples;
    float v_raw_instant = (adc_avg * VREF) / 4095.0f;

    // Đưa qua bộ lọc thông thấp để giữ độ mịn đồ thị
    float v_out_smooth = Low_Pass_Filter(v_raw_instant, v_out_pin);

    // TIẾN HÀNH LÀM TRÒN: Giữ lại đúng 2 chữ số thập phân (Ví dụ: 2.37346... -> 2.37)
    v_out_pin = ((float)((int)(v_out_smooth * 100.0f + 0.5f))) / 100.0f;

    // Lúc này nhân hệ số lên thì v_in_real sẽ cực kỳ ổn định, không bị gợn số lẻ phía sau
    v_in_real = v_out_pin * 3.3f;
    // --- 4. ĐÓNG GÓI VÀ GỬI CAN BUS + IN LOG UART CHU KỲ 1 GIÂY ---
    if (HAL_GetTick() - last_send_time >= 1000)
    {
      last_send_time = HAL_GetTick();

      // In thông tin debug lên màn hình Terminal thông qua UART
      sprintf(uart_buf, "[LOG] RPM: %.1f | Khoang cach: %.1f cm | ADC Volts: %.2fV\r\n",
              motor_rpm, distance_cm, v_in_real);
      HAL_UART_Transmit(&huart1, (uint8_t *)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);

      /* ĐÓNG GÓI DỮ LIỆU VÀO FRAME CAN
         Do dữ liệu dạng Float chiếm 4 bytes, ta sẽ nhân giá trị với một hệ số (scale)
         rồi ép kiểu về số nguyên để truyền đi gọn và chính xác nhất.
      */
      int16_t can_rpm = (int16_t)motor_rpm;               // Tốc độ (vòng/phút)
      int16_t can_distance = (int16_t)(distance_cm * 10); // Nhân 10 để giữ 1 chữ số thập phân (ví dụ: 25.4 cm -> 254)
      int16_t can_voltage = (int16_t)(v_in_real * 100);   // Nhân 100 để giữ 2 chữ số thập phân (ví dụ: 3.25V -> 325)

      tx_msg.can_id = 0x3A0; // Chọn một CAN ID tiêu chuẩn đại diện cho cụm cảm biến này
      tx_msg.can_dlc = 6;    // Tổng cộng gửi đi 6 bytes dữ liệu

      // Tách dữ liệu 16-bit thành các byte đơn để đưa vào mảng DATA
      tx_msg.data[0] = (uint8_t)(can_rpm >> 8);
      tx_msg.data[1] = (uint8_t)(can_rpm & 0xFF);
      tx_msg.data[2] = (uint8_t)(can_distance >> 8);
      tx_msg.data[3] = (uint8_t)(can_distance & 0xFF);
      tx_msg.data[4] = (uint8_t)(can_voltage >> 8);
      tx_msg.data[5] = (uint8_t)(can_voltage & 0xFF);

      // Tiến hành gửi gói tin lên mạng CAN Bus
      eERROR tx_status = sendMessages(&tx_msg);
      if (tx_status == ERROR_OK)
      {
        sprintf(uart_buf, " -> [CAN] Gui goi tin cam bien THANH CONG!\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t *)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);
      }
      else
      {
        sprintf(uart_buf, " -> [CAN] Gui THAT BAI (Ma loi: %d)\r\n", tx_status);
        HAL_UART_Transmit(&huart1, (uint8_t *)uart_buf, strlen(uart_buf), HAL_MAX_DELAY);
      }
    }

    // Delay ngắn để tránh quá tải CPU và cho cảm biến siêu âm nghỉ giữa các chu kỳ đo ngắn
    HAL_Delay(60);
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
   */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
   */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */
}

/**
 * @brief SPI1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */
}

/**
 * @brief TIM2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
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
  if (HAL_UART_Init(&huart1) != HAL_OK)
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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PA4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB11 */
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
#if 1
#include <stdio.h>

int fputc(int ch, FILE *f)
{
  // Xuất ký tự trực tiếp qua UART1
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
#endif
// -----------------------------------------------------------------
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_0)
  {
    pulse_count++; // Tăng tổng số xung khi đĩa quay qua cảm biến encoder
  }
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 * where the assert_param error has occurred.
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