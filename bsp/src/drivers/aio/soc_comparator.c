/*
 * Copyright (c) 2015, Intel Corporation. All rights reserved.
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

#include <init.h>

#include "drivers/soc_comparator.h"

#include "infra/log.h"
#include "infra/device.h"
#include "machine.h"

void comp_isr()
{
	struct cmp_cb *cmp_cb_tmp;
	struct td_device *comparator_dev = &pf_device_soc_comparator;
	int i;

	cmp_cb_tmp = (struct cmp_cb *)(comparator_dev->priv);

	for (i = 0; i < CMP_COUNT; i++) {
		if (MMIO_REG_VAL(CMP_STAT_CLR) & (1 << i)) {
			comp_disable(i);
			if (cmp_cb_tmp[i].cb != NULL) {
				cmp_cb_tmp[i].cb(cmp_cb_tmp[i].cb_data);
			} else {
				pr_debug(LOG_MODULE_DRV,
					 "Unexpected comparator interrupt: %d",
					 i);
			}
			// Clear interrupt status to process next IRQ
			MMIO_REG_VAL(CMP_STAT_CLR) |= (1 << i);
		}
	}
}

DRIVER_API_RC comp_configure(struct td_device *dev, int index, int polarity,
			     int refsel, void (*cb)(void *), void *param)
{
	struct cmp_cb *cmp_cb_tmp = (struct cmp_cb *)dev->priv;

	if (index >= CMP_COUNT || index < 0) {
		return DRV_RC_INVALID_CONFIG;
	}
	cmp_cb_tmp[index].cb = cb;
	cmp_cb_tmp[index].cb_data = param;

	// Enable comparator <index>
	MMIO_REG_VAL(CMP_PWR) |= (1 << index);

	if (polarity)
		MMIO_REG_VAL(CMP_REF_POL) |= (1 << index);
	else
		MMIO_REG_VAL(CMP_REF_POL) &= ~(1 << index);
	if (refsel)
		MMIO_REG_VAL(CMP_REF_SEL) |= (1 << index);
	else
		MMIO_REG_VAL(CMP_REF_SEL) &= ~(1 << index);

	// Enable power in comparator <index>
	MMIO_REG_VAL(CMP_EN) |= (1 << index);

	/* Enable interrupt to host. */
#ifdef CONFIG_QUARK
	MMIO_REG_VAL_FROM_BASE(SCSS_REGISTER_BASE,
			       INT_COMPARATORS_HOST_MASK) &=
		~(1 << index);
#elif CONFIG_ARC
	MMIO_REG_VAL_FROM_BASE(SCSS_REGISTER_BASE, INT_COMPARATORS_SS_MASK) &=
		~(1 << index);
#else
#error Unknown target for comparator driver
#endif
	return DRV_RC_OK;
}

DRIVER_API_RC comp_disable(int index)
{
	if (index >= CMP_COUNT || index < 0) {
		return DRV_RC_INVALID_CONFIG;
	}
	// Disable comparator <index>
	MMIO_REG_VAL(CMP_EN) &= ~(1 << index);
	// Disable power in comparator <index>
	MMIO_REG_VAL(CMP_PWR) &= ~(1 << index);

	return DRV_RC_OK;
}

static int comp_init(struct td_device *dev)
{
	int i;
	struct cmp_cb *cmp_cb_tmp = (struct cmp_cb *)dev->priv;

	for (i = 0; i < CMP_COUNT; i++) {
		cmp_cb_tmp[i].cb = NULL;
		cmp_cb_tmp[i].cb_data = NULL;
	}
	dev->priv = cmp_cb_tmp;
#ifdef CONFIG_QUARK
	MMIO_REG_VAL_FROM_BASE(SCSS_REGISTER_BASE,
			       INT_COMPARATORS_HOST_MASK) |=
		INT_COMPARATORS_MASK;
#elif CONFIG_ARC
	MMIO_REG_VAL_FROM_BASE(SCSS_REGISTER_BASE, INT_COMPARATORS_SS_MASK) |=
		INT_COMPARATORS_MASK;
#endif
	MMIO_REG_VAL(CMP_STAT_CLR) |= INT_COMPARATORS_MASK;
	MMIO_REG_VAL(CMP_EN) &= ~(INT_COMPARATORS_MASK);
	MMIO_REG_VAL(CMP_PWR) &= ~(INT_COMPARATORS_MASK);

	/* enable interrupt trap for comparator driver */
	IRQ_CONNECT(SOC_CMP_INTERRUPT, ISR_DEFAULT_PRIO, comp_isr, NULL, 0);
	irq_enable(SOC_CMP_INTERRUPT);
	return 0;
}

static int comp_resume(struct td_device *dev)
{
	irq_enable(SOC_CMP_INTERRUPT);
	// Clear interrupt flag to enable interrupts after resume
	MMIO_REG_VAL(CMP_STAT_CLR) = ~0;
	return 0;
}

struct driver soc_comparator_driver = {
	.init = comp_init,
	.suspend = NULL,
	.resume = comp_resume
};
