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

#ifndef BLE_APP_H_
#define BLE_APP_H_

#include <stdbool.h>
#include <stdint.h>

// for T_QUEUE
#include "os/os.h"

// for bt_addr_le_t
#include "bluetooth/hci.h"
// for bt_le_conn_param
#include "bluetooth/conn.h"

/**
 * @defgroup ble_app_lib BLE Application library
 * Interface with low level BLE stack.
 *
 * <table>
 * <tr><th><b>Include file</b><td><tt> \#include "framework/include/lib/ble/ble_app.h"</tt>
 * <tr><th><b>Source path</b> <td><tt>framework/src/lib/ble</tt>
 * <tr><th><b>Config flag</b> <td><tt>BLE_APP</tt>
 * </table>
 *
 * The BLE Application library provides an API to:
 * - start the BLE stack and register the services @ref ble_start_app
 * - get/set the device name @ref ble_app_get_device_name / @ref ble_app_set_device_name
 * - get the device address @ref ble_app_get_device_bda
 * - manage advertising @ref ble_app_start_advertisement / @ref ble_app_stop_advertisement
 * - manage connections and connection parameter updates @ref ble_app_conn_update
 * - clear bonding information @ref ble_app_clear_bonds
 *
 * Several advertising modes are supported and interval value is configurable:
 * - Ultra fast (`CONFIG_BLE_ULTRA_FAST_ADV_INTERVAL` option)
 * - Normal (`CONFIG_BLE_APP_DEFAULT_ADV_INTERVAL` option)
 * - Slow (`CONFIG_BLE_ADV_SLOW_INTERVAL` option)
 *
 * By default, once initialized, the BLE application starts ultra fast advertising.
 * After `CONFIG_BLE_ADV_FAST_TIMEOUT` seconds it switches to normal advertising mode.
 * After `CONFIG_BLE_APP_DEFAULT_ADV_TIMEOUT` it switches to slow advertising mode.
 * After a disconnection, it starts normal advertising mode.
 *
 * Default connection interval is 150ms. It can be dynamically updated by using
 * @ref ble_app_conn_update.
 *
 *
 * @ingroup ble_stack
 * @{
 */

/**
 * Get the device bluetooth address.
 *
 * @return Pointer to the bda addressble_app_set_device_name
 */
const bt_addr_le_t *ble_app_get_device_bda(void);

/**
 * Get the device name.
 *
 * @return Pointer to the NULL terminated string containing the device name
 */
const uint8_t *ble_app_get_device_name(void);

/**
 * Set the device name.
 *
 * @param p_device_name Pointer to the NULL terminated string
 * @param store true if the name should be stored permanently
 *
 * @return 0 or positive value in case of success or a negative error code
 */
int ble_app_set_device_name(const uint8_t *p_device_name, bool store);

/**
 * Start BLE application.
 *
 * Register and enable BLE and starts advertising depending on BLE service
 * internal state. Messages will be treated internally
 *
 * @param queue queue on which CFW messages will be forwarded.
 *
 */
void ble_start_app(T_QUEUE queue);

/**
 * Update BLE application handle parameters.
 *
 * This function should be called before after the BLE app connection is
 * established, so you can update it with specific parameters. It can be used in
 * very specific scenarios, e.g.: if you need specific tuning for the BLE
 * connection.
 *
 * @param p_params ble specific parameters
 * @return connection update return value, -1 if the connection is not ready.
 */
int ble_app_conn_update(const struct bt_le_conn_param *p_params);

/**
 * Restore default BLE application connection parameters.
 *
 * This function should be called to restore the default connection parameters.
 * It can be used in very specific scenarios, e.g. if you need specific
 * tuning for the BLE connection for a while and once the work is over you could
 * restore the default parameter.
 *
 * @return connection update return value, -1 if the connection is not ready.
 */
int ble_app_restore_default_conn(void);

/**
 * Start advertisement reason.
 */
enum ble_adv_reason {
	BLE_ADV_STARTUP,
	BLE_ADV_TIMEOUT,
	BLE_ADV_DISCONNECT,
	BLE_ADV_USER1,
	BLE_ADV_USER2,
};

/**
 * Start advertisement based on security status
 *
 * This function starts the advertisement with the given options. The start may
 * return an error event if an advertisement is already ongoing.
 *
 * @param reason advertisement reason
 */
void ble_app_start_advertisement(enum ble_adv_reason reason);

/**
 * Stop advertisement.
 *
 * This function stops the advertisement. If no advertisement is going, the app
 * receives an error status which can be ignored.
 *
 */
void ble_app_stop_advertisement(void);

/**
 * Clear all BLE bonding information (linkkeys etc).
 */
void ble_app_clear_bonds(void);

/**
 * BLE properties ID for storage
 */
enum ble_app_prop_id {
	BLE_PROPERTY_ID_DEVICE_NAME = 0,
	BLE_PROPERTY_ID_SM_CONFIG,
	BLE_PROPERTY_ID_UAS_CLIENTS,
};

/**
 * Flags for properties storage BLE app helper. It is possible to combine ADD
 * and WRITE flags if the caller does not know if the property has already been
 * added.
 */
enum ble_app_prop_write_flags {
	BLE_APP_PROP_ADD                = 1, /**< Add in non persistent storage */
	BLE_APP_PROP_ADD_PERSISTENT     = 2, /**< Add in persistent storage */
	BLE_APP_PROP_WRITE              = 4, /**< Update existing properties */
};

/**
 * Callback type for property read event
 */
typedef void (*ble_app_prop_rd_cb_t)(void *buf, uint16_t len, intptr_t priv);

/**
 * Read a BLE property from flash.
 *
 * @param prop_id	BLE ID of property to read (@ref ble_app_prop_id)
 * @param cb		Callback called once read completed.
 * @param priv		Private data callback
 *
 * @return 0 if successful
 */
int ble_app_prop_read(uint16_t prop_id, ble_app_prop_rd_cb_t cb, intptr_t priv);

/**
 * Save a BLE property to flash.
 *
 * @param prop_id	BLE ID of property to save (@ref ble_app_prop_id)
 * @param buf		Buffer to save
 * @param len		Buffer length
 * @param flags		Bitfield of @ref ble_app_prop_write_flags. ADD and WRITE
 *                      flags can be used simultaneously.
 *
 * @return 0 if successful
 */
int ble_app_prop_write(uint16_t prop_id, void *buf, uint16_t len,
		       uint32_t flags);

/**
 * Callback for advertisement timeout.
 */
typedef void (*ble_adv_timeout_cb_t)(void);

/**
 * Start advertising timer.
 *
 * @param timeout	Timeout in seconds
 * @param cb		Timeout callback function
 */
void ble_app_adv_timer_start(uint16_t timeout, ble_adv_timeout_cb_t cb);

/**
 * This function deletes the advertising timer.
 */
void ble_app_adv_timer_delete(void);

/**
 * Function that is called by the ble_app module when the initialization has
 * completed. This function is project specific and should be implemented in
 * the project to perform specific actions such as add BLE services.
 *
 *
 */
void on_ble_app_started(void);

/**
 * @}
 */
#endif /* BLE_APP_H_ */
