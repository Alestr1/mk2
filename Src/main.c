/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (МК2 Вариант 2: Таймер со звуком)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "kb.h"
#include "pca9538.h"
#include "oled.h"
#include "fonts.h"
#include "sdk_uart.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
#define STATE_SET    0
#define STATE_RUN    1
#define STATE_ALARM  2

uint8_t timer_state = STATE_SET;
uint32_t timer_value = 0;
uint32_t last_tick = 0;
uint32_t beep_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
char get_key_pressed(void);
void update_display(void);
void format_num(uint32_t num, char* buf);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void format_num(uint32_t num, char* buf) {
    buf[0] = '0' + (num / 1000) % 10;
    buf[1] = '0' + (num / 100) % 10;
    buf[2] = '0' + (num / 10) % 10;
    buf[3] = '0' + num % 10;
    buf[4] = '\0';
}

char get_key_pressed(void) {
    uint8_t rows[] = {ROW1, ROW2, ROW3, ROW4};
    char keymap[4][3] = {
        {'1', '2', '3'},
        {'4', '5', '6'},
        {'7', '8', '9'},
        {'*', '0', '#'}
    };
    
    for(int i = 0; i < 4; i++) {
        uint8_t key_code = Check_Row(rows[i]);
        if(key_code != 0x00) {
            HAL_Delay(50);
            if(Check_Row(rows[i]) == key_code) {
                uint32_t wait_start = HAL_GetTick();
                while(Check_Row(rows[i]) != 0x00) {
                    if (HAL_GetTick() - wait_start > 1000) break;
                    HAL_Delay(10);
                }
                if(key_code == 0x04) return keymap[i][0];
                if(key_code == 0x02) return keymap[i][1];
                if(key_code == 0x01) return keymap[i][2];
            }
        }
    }
    return '\0';
}

void update_display(void) {
    char num_buf[5];
    format_num(timer_value, num_buf);

    oled_Fill(Black);
    oled_SetCursor(0, 0);
    
    if (timer_state == STATE_SET) {
        oled_WriteString("SET: ", Font_7x10, White);
        oled_WriteString(num_buf, Font_7x10, White);
        oled_WriteString(" s", Font_7x10, White);
        oled_SetCursor(0, 20);
        oled_WriteString("#:Start  *:Clear", Font_7x10, White);
    } 
    else if (timer_state == STATE_RUN) {
        oled_WriteString("RUN: ", Font_7x10, White);
        oled_WriteString(num_buf, Font_7x10, White);
        oled_WriteString(" s", Font_7x10, White);
    } 
    else if (timer_state == STATE_ALARM) {
        oled_WriteString("TIME IS UP!", Font_7x10, White);
        oled_SetCursor(0, 20);
        oled_WriteString("*: Reset", Font_7x10, White);
    }
    oled_UpdateScreen();
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
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART6_UART_Init();
  
  /* USER CODE BEGIN 2 */
  // 1. Инициализация клавиатуры и дисплея
  Set_Keyboard();
  oled_Init();

  // 2. Настройка пина динамика (PE9) как выход
  __HAL_RCC_GPIOE_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);
  
  update_display();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    char key = get_key_pressed();

    // --- 1. Обработка кнопок ---
    if (key != '\0') {
        if (timer_state == STATE_SET) {
            if (key >= '0' && key <= '9') {
                timer_value = timer_value * 10 + (key - '0');
                if (timer_value > 9999) timer_value = 9999;
                update_display();
            } 
            else if (key == '#' && timer_value > 0) {
                timer_state = STATE_RUN;
                last_tick = HAL_GetTick();
                update_display();
            } 
            else if (key == '*') {
                timer_value = 0;
                update_display();
            }
        } 
        else if (timer_state == STATE_RUN) {
            if (key == '*') { // Сброс во время работы
                timer_state = STATE_SET;
                timer_value = 0;
                update_display();
            }
        } 
        else if (timer_state == STATE_ALARM) {
            if (key == '*') { // Сброс сирены
                timer_state = STATE_SET;
                timer_value = 0;
                HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET); // Выкл звук
                update_display();
            }
        }
    }

    // --- 2. Логика обратного отсчета (неблокирующая) ---
    if (timer_state == STATE_RUN) {
        if (HAL_GetTick() - last_tick >= 1000) {
            last_tick += 1000;
            if (timer_value > 0) {
                timer_value--;
                update_display();
            }
            if (timer_value == 0) {
                timer_state = STATE_ALARM;
                update_display();
            }
        }
    }

    // --- 3. Генерация звука (программный меандр ~500 Гц) ---
    if (timer_state == STATE_ALARM) {
        // Переключаем пин каждые 1 мс (период 2 мс = 500 Гц)
        if (HAL_GetTick() - beep_tick > 1) {
            beep_tick = HAL_GetTick();
            HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_9);
        }
    }

    /* USER CODE END WHILE */
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

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
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
  __disable_irq();
  while (1) { }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
