/**
 * Copyright (c) 2014 - 2020, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/** @file
 *
 * @defgroup blinky_example_main main.c
 * @{
 * @ingroup blinky_example
 * @brief Blinky Example Application main file.
 *
 * This file contains the source code for a sample application to blink LEDs.
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "boards.h"
#include "app_error.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "nrf_saadc.h"
#include "nrf_drv_saadc.h"

#include "nrf_drv_gpiote.h"

#include "twi_master.h"

#include "SEGGER_RTT.h"
#include "nrf_drv_timer.h"
#include "nrf_ppi.h"

#define SAMPLES_IN_BUFFER 2
static nrf_saadc_value_t     m_buffer_pool[2][SAMPLES_IN_BUFFER];
static uint32_t              m_adc_evt_counter;

#ifdef BSP_BUTTON_0 
#define PIN_IN BSP_BUTTON_0
#endif
#ifndef PIN_IN
#error "please indicate input pin"
#endif

#ifdef BSP_LED_0
#define PIN_OUT BSP_LED_0
#endif
#ifndef PIN_OUT
#error "Please indicate output pin"
#endif

#define TOUCHPAD_INT_PIN       26

#define TOUCHPAD_RESET_PIN     8

#define TP_HRS_DEVICE_WRITE_ADDRESS 0x2A // 0x2A<<1
#define TP_HRS_DEVICE_READ_ADDRESS 0x2B  // 0x2B<<1

static bool touchpad_read(uint8_t* Data, uint8_t RegAddr)
{
	uint8_t data[2] = { 0 };
	data[0] = RegAddr;
	twi_master_transfer(TP_HRS_DEVICE_WRITE_ADDRESS, data, 1, false);
	twi_master_transfer(TP_HRS_DEVICE_READ_ADDRESS, Data, 1, true);
	return true;
}

bool touchpad_check_id(void)
{
	uint8_t id = 0;
	touchpad_read(&id, 0xA7);
	SEGGER_RTT_printf(0, "touch id:%02x\n", id);
#ifdef CTS_716
	if (id == 0x20)
#else
	if (id == 0xb4)
#endif
	{
		return true;
	}
	else
	{
		return false;
	}
}

static void touchpad_reset(void)
{
	nrf_gpio_pin_set(TOUCHPAD_RESET_PIN);
	nrf_delay_ms(100);
	nrf_gpio_pin_clear(TOUCHPAD_RESET_PIN);
	nrf_delay_ms(100);
	//nrf_gpio_pin_set(TOUCHPAD_RESET_PIN);
	//nrf_delay_ms(100);
}

void in_pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{

	uint8_t tp_temp[10] = { 0 };
	touchpad_read(&tp_temp[0], 0x01);
	touchpad_read(&tp_temp[1], 0x02);

	touchpad_read(&tp_temp[2], 0x04);
	touchpad_read(&tp_temp[3], 0x06);

	SEGGER_RTT_printf(0, "x:%d,y:%d\n", tp_temp[2], tp_temp[3]);

	switch (tp_temp[0])
	{
	case 0x01:
	{
		SEGGER_RTT_printf(0, "up:%x", tp_temp[0]);
	}
	break;
	case 0x02:
	{
		SEGGER_RTT_printf(0, "down:%x", tp_temp[0]);
	}
	break;
	case 0x03:
	{
		SEGGER_RTT_printf(0, "right:%x", tp_temp[0]);
	}
	break;
	case 0x04:
	{
		SEGGER_RTT_printf(0, "left:%x", tp_temp[0]);
	}
	break;
	case 0x05:
	{
		SEGGER_RTT_printf(0, "short tap:%x", tp_temp[0]);
	}
	break;
	case 0x0c:
	{
		SEGGER_RTT_printf(0, "long tap:%x", tp_temp[0]);
	}
	break;
        }
}

static void gpio_init(void)
{
	ret_code_t err_code;

	err_code = nrf_drv_gpiote_init();
	APP_ERROR_CHECK(err_code);

	nrf_drv_gpiote_in_config_t in_config = GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);
	in_config.pull = NRF_GPIO_PIN_PULLUP;

	err_code = nrf_drv_gpiote_in_init(PIN_IN, &in_config, in_pin_handler);
	APP_ERROR_CHECK(err_code);

	nrf_drv_gpiote_in_event_enable(PIN_IN, true);
}

void saadc_callback(nrf_drv_saadc_evt_t const* p_event)
{
    if(p_event -> type == NRF_DRV_SAADC_EVT_DONE)
    {
        ret_code_t err_code;

        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer,SAMPLES_IN_BUFFER);
        APP_ERROR_CHECK(err_code);

        int i;
        SEGGER_RTT_printf(0,"ADC event number: %d",(int)m_adc_evt_counter);

        for(i=0;i<SAMPLES_IN_BUFFER;i++)
        {
            SEGGER_RTT_printf(0,"%d",p_event->data.done.p_buffer[i]);
        }
        m_adc_evt_counter++;
    }
}


void saadc_init(void)
{
    ret_code_t err_code;

    err_code = nrf_drv_saadc_init(NULL,saadc_callback);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[0],SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(m_buffer_pool[1],SAMPLES_IN_BUFFER);
    APP_ERROR_CHECK(err_code);
}

int main(void)
{
        gpio_init();
	twi_master_init();
	nrf_gpio_cfg_output(TOUCHPAD_RESET_PIN);
	touchpad_reset();
	touchpad_check_id();

	
        saadc_init();
	int count = 0;
	while (true)
	{

		count++;
		nrf_delay_ms(3000);


		nrf_gpio_cfg_output(7);
		if (count % 2 == 1)
		{
			nrf_gpio_pin_write(7, 1);
		}
		else
		{
			nrf_gpio_pin_write(7, 0);
		}

	}
	//leds_init();
	/* Configure board. */
	bsp_board_init(BSP_INIT_LEDS);
	ret_code_t err_code = NRF_LOG_INIT(NULL);
	APP_ERROR_CHECK(err_code);

	NRF_LOG_DEFAULT_BACKENDS_INIT();

	/* Toggle LEDs. */
	while (true)
	{
		for (int i = 0; i < LEDS_NUMBER; i++)
		{
			//bsp_board_led_invert(i);
			nrf_delay_ms(500);
			NRF_LOG_INFO("hello world222 %d", i);
			NRF_LOG_FLUSH();
		}
	}
	return 0;
}

/**
 *@}
 **/
