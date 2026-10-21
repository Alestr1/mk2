/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body (Таймер ЧЧ:ММ:СС + Аппаратный ШИМ для PE9)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#include "stm32f4xx_hal_tim.h"

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

uint32_t input_raw = 0;
uint8_t input_digits = 0;

TIM_HandleTypeDef htim1;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
char get_key_pressed(void);
void update_display(void);
void format_hms_input(uint32_t raw, uint8_t digits, char* buf);
void format_hms_total(uint32_t total_sec, char* buf);
void MX_TIM1_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void format_hms_input(uint32_t raw, uint8_t digits, char* buf) {
    uint32_t h = 0, m = 0, s = 0;
    if (digits == 1 || digits == 2) {
        s = raw;
    } else if (digits == 3 || digits == 4) {
        m = raw / 100;
        s = raw % 100;
    } else if (digits == 5 || digits == 6) {
        h = raw / 10000;
        m = (raw / 100) % 100;
        s = raw % 100;
    }

    buf[0] = '0' + (h / 10) % 10;
    buf[1] = '0' + h % 10;
    buf[2] = ':';
    buf[3] = '0' + (m / 10) % 10;
    buf[4] = '0' + m % 10;
    buf[5] = ':';
    buf[6] = '0' + (s / 10) % 10;
    buf[7] = '0' + s % 10;
    buf[8] = '\0';
}

void format_hms_total(uint32_t total_sec, char* buf) {
    uint32_t h = total_sec / 3600;
    uint32_t m = (total_sec % 3600) / 60;
    uint32_t s = total_sec % 60;

    if (h > 99) h = 99;

    buf[0] = '0' + (h / 10) % 10;
    buf[1] = '0' + h % 10;
    buf[2] = ':';
    buf[3] = '0' + (m / 10) % 10;
    buf[4] = '0' + m % 10;
    buf[5] = ':';
    buf[6] = '0' + (s / 10) % 10;
    buf[7] = '0' + s % 10;
    buf[8] = '\0';
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
    char time_buf[9];

    oled_Fill(Black);
    oled_SetCursor(0, 0);
    
    if (timer_state == STATE_SET) {
        format_hms_input(input_raw, input_digits, time_buf);
        oled_WriteString("SET: ", Font_7x10, White);
        oled_WriteString(time_buf, Font_7x10, White);
        oled_SetCursor(0, 20);
        oled_WriteString("#:Start  *:Clear", Font_7x10, White);
    } 
    else if (timer_state == STATE_RUN) {
        format_hms_total(timer_value, time_buf);
        oled_WriteString("RUN: ", Font_7x10, White);
        oled_WriteString(time_buf, Font_7x10, White);
    } 
    else if (timer_state == STATE_ALARM) {
        oled_WriteString("TIME IS UP!", Font_7x10, White);
        oled_SetCursor(0, 20);
        oled_WriteString("*: Reset", Font_7x10, White);
    }
    oled_UpdateScreen();
}

// --- ИНИЦИАЛИЗАЦИЯ TIM1 ДЛЯ ЗУММЕРА (PE9 = TIM1_CH1) ---
void MX_TIM1_Init(void) {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    htim1.Instance = TIM1; // <-- ИЗМЕНЕНО: TIM1
    htim1.Init.Prescaler = 84 - 1;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 200 - 1;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&htim1);

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
}

void Buzzer_On(void) {
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 100);
}

void Buzzer_Off(void) {
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
}
/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART6_UART_Init();
  
  /* USER CODE BEGIN 2 */
  Set_Keyboard();
  oled_Init();
  
  MX_TIM1_Init(); // <-- ИЗМЕНЕНО

  update_display();
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    char key = get_key_pressed();

    if (key != '\0') {
        if (timer_state == STATE_SET) {
            if (key >= '0' && key <= '9') {
                if (input_digits < 6) {
                    input_raw = input_raw * 10 + (key - '0');
                    input_digits++;
                }
                update_display();
            } 
            else if (key == '#') {
                uint32_t h = 0, m = 0, s = 0;
                uint8_t is_valid = 1;

                if (input_digits == 1 || input_digits == 2) {
                    s = input_raw;
                    if (s >= 60) is_valid = 0;
                } else if (input_digits == 3 || input_digits == 4) {
                    m = input_raw / 100;
                    s = input_raw % 100;
                    if (m >= 60 || s >= 60) is_valid = 0;
                } else if (input_digits == 5 || input_digits == 6) {
                    h = input_raw / 10000;
                    m = (input_raw / 100) % 100;
                    s = input_raw % 100;
                    if (m >= 60 || s >= 60) is_valid = 0;
                } else {
                    is_valid = 0;
                }

                if (is_valid) {
                    timer_value = (h * 3600) + (m * 60) + s;
                    if (timer_value > 0) {
                        timer_state = STATE_RUN;
                        last_tick = HAL_GetTick();
                        update_display();
                    } else {
                        input_raw = 0;
                        input_digits = 0;
                        update_display();
                    }
                } else {
                    input_raw = 0;
                    input_digits = 0;
                    update_display();
                }
            } 
            else if (key == '*') {
                input_raw = 0;
                input_digits = 0;
                timer_value = 0;
                update_display();
            }
        } 
        else if (timer_state == STATE_RUN) {
            if (key == '*') {
                timer_state = STATE_SET;
                timer_value = 0;
                input_raw = 0;
                input_digits = 0;
                update_display();
            }
        } 
        else if (timer_state == STATE_ALARM) {
            if (key == '*') {
                timer_state = STATE_SET;
                timer_value = 0;
                input_raw = 0;
                input_digits = 0;
                Buzzer_Off();
                update_display();
            }
        }
    }

    if (timer_state == STATE_RUN) {
        if (HAL_GetTick() - last_tick >= 1000) {
            last_tick += 1000;
            if (timer_value > 0) {
                timer_value--;
                update_display();
            }
            if (timer_value == 0) {
                timer_state = STATE_ALARM;
                Buzzer_On();
                update_display();
            }
        }
    }

    /* USER CODE END WHILE */
  }
  /* USER CODE END 3 */
}

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

void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
