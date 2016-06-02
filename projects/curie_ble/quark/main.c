/*
 * Copyright (c) 2016, Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* infra */
#include "infra/log.h"
#include "infra/bsp.h"
#include "infra/xloop.h"
#include "cfw/cfw.h"
/* Watchdog helper */
#include "infra/wdt_helper.h"
/* BLE services helper */
#include "lib/ble/ble_app.h"
#include "sensordata.h"


/* System main queue it will be used on the component framework to add messages
 * on it. */
static T_QUEUE queue;
static xloop_t loop;

int wdt_func(void *param)
{
	/* Acknowledge the system watchdog to prevent panic and reset */
	wdt_keepalive();
	return 0; /* Continue */
}

void main_task(void *param)
{
	/* Init BSP (also init BSP on ARC core) */
	queue = bsp_init();
	pr_info(LOG_MODULE_MAIN, "BSP init done");

	/* start Quark watchdog */
	wdt_start(WDT_MAX_TIMEOUT_MS);

	/* Init the CFW */
	cfw_init(queue);
	pr_info(LOG_MODULE_MAIN, "CFW init done");

#ifdef CONFIG_BLE_APP
	ble_start_app(queue);
#endif
	/* Initialize sensordata client */
	sensordata_init(queue);
	pr_info(LOG_MODULE_MAIN, "sensor service init in progress...");

	xloop_init_from_queue(&loop, queue);
	xloop_post_func_periodic(&loop, wdt_func, NULL, WDT_MAX_TIMEOUT_MS / 2);
	xloop_run(&loop);
}
