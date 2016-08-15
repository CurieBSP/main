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
#include "lib/ble/ble_app.h"

#include <string.h>
#include <errno.h>
#include <misc/byteorder.h>
#include "util/assert.h"
#include "services/ble_service/ble_service.h"
#include "services/properties_service/properties_service.h"
#ifdef CONFIG_BLE_APP_USE_BAT
#include "services/battery_service/battery_service.h"
#endif
#include "ble_service_utils.h"
#include "infra/log.h"
#include "ble_protocol.h"
#include "lib/ble/tcmd/ble_tcmd.h"
#include "lib/ble/bas/ble_bas.h"
#include "lib/ble/dis/ble_dis.h"
#include "lib/ble/gap/ble_gap.h"
#include "lib/ble/hrs/ble_hrs.h"
#if defined(CONFIG_PACKAGE_ISPP)
#include "ble_ispp.h"
#endif
#include "lib/ble/lns/ble_lns.h"
#include "lib/ble/rscs/ble_rscs.h"
#if defined(CONFIG_UAS)
#include "ble_uas.h"
#endif

#include "infra/factory_data.h"
#include "infra/version.h"
#include "curie_factory_data.h"
#ifdef CONFIG_SYSTEM_EVENTS
#include "infra/system_events.h"
#endif
#include "util/misc.h"
/* boot reason */
#include "infra/boot.h"

/* NBLE specific */
#include "gap_internal.h"

/*
 * Local macros definition
 */

/* enable ble_app.c debug */
/*#define BLE_APP_DEBUG */

/* Connection parameters used for Peripheral Preferred Connection Parameters (PPCP) and update request */
#define MIN_CONN_INTERVAL MSEC_TO_1_25_MS_UNITS(CONFIG_BLE_MIN_CONN_INTERVAL)
#define MAX_CONN_INTERVAL MSEC_TO_1_25_MS_UNITS(CONFIG_BLE_MAX_CONN_INTERVAL)
#define SLAVE_LATENCY CONFIG_BLE_SLAVE_LATENCY
#define CONN_SUP_TIMEOUT MSEC_TO_10_MS_UNITS(CONFIG_BLE_CONN_SUP_TIMEOUT)

#define BLE_APP_APPEARANCE 192
#define BLE_APP_MANUFACTURER 2

#define MANUFACTURER_NAME "IntelCorp"
#define MODEL_NUM         "Curie"
#define HARDWARE_REV       "1.0"

#define BLE_DEVICE_NAME_WRITE_PERM GAP_SEC_NO_PERMISSION

/*
 * Local structures definition
 */

enum ble_app_flags {
	BLE_APP_ENABLED = 1,
};

struct ble_app_cb {
	cfw_service_conn_t *p_ble_conn;
	cfw_service_conn_t *p_props_conn;
#ifdef CONFIG_BLE_APP_USE_BAT
	cfw_service_conn_t *p_batt_conn;
#endif
	struct bt_conn *conn_periph; /* Current connection reference */
	T_TIMER conn_timer;
	T_TIMER adv_timer;
	uint32_t adv_timeout;
	struct ble_connection_values conn_values;
	bt_addr_le_t my_bd_addr;
	/* the name must be stored in FULL because the property interface is asynchronous */
	uint8_t device_name[BLE_MAX_DEVICE_NAME + 1];
	/* Bitfield of ble_app_flags indicating the state */
	uint8_t flags;
};

static struct ble_app_cb _ble_app_cb = { 0 };

/* Internal msg for property storage helper */
struct ble_app_prop_rd_msg {
	ble_app_prop_rd_cb_t cb;
	intptr_t priv;
};

/*
 * Local functions declaration
 */
static void ble_app_delete_conn_timer(void);

/*
 * Functions definition
 */
int ble_app_conn_update(const struct bt_le_conn_param *p_params)
{
	ble_app_delete_conn_timer();

	if (_ble_app_cb.flags & BLE_APP_ENABLED)
		return bt_conn_le_param_update(_ble_app_cb.conn_periph,
					       p_params);
	else
		return -1;
}

/** Helper function. */
static ssize_t read_string(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr,
			   void *buf, uint16_t buf_len,
			   uint16_t offset,
			   const char *value)
{
	size_t str_len = strlen(value);

	return bt_gatt_attr_read(conn, attr, buf, buf_len, offset, value,
				 str_len);
}

int ble_app_prop_read(uint16_t prop_id, ble_app_prop_rd_cb_t cb, intptr_t priv)
{
	struct ble_app_prop_rd_msg *msg = balloc(sizeof(*msg), NULL);

	if (!_ble_app_cb.p_props_conn)
		return -1;

	msg->cb = cb;
	msg->priv = priv;

	properties_service_read(_ble_app_cb.p_props_conn, BLE_SERVICE_ID,
				prop_id,
				msg);
	return 0;
}

int ble_app_prop_write(uint16_t prop_id, void *buf, uint16_t len,
		       uint32_t flags)
{
	if (!_ble_app_cb.p_props_conn)
		return -1;

	assert(flags);

	if (flags & (BLE_APP_PROP_ADD | BLE_APP_PROP_ADD_PERSISTENT))
		properties_service_add(_ble_app_cb.p_props_conn, BLE_SERVICE_ID,
				       prop_id,
				       flags & BLE_APP_PROP_ADD_PERSISTENT, buf,
				       len,
				       NULL);
	if (flags & BLE_APP_PROP_WRITE)
		properties_service_write(_ble_app_cb.p_props_conn,
					 BLE_SERVICE_ID, prop_id,
					 buf, len,
					 NULL);
	return 0;
}

/*
 * The following functions overwrite the default functions defined in
 * GAP and DIS services
 */
ssize_t on_gap_rd_device_name(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      void *buf, uint16_t len,
			      uint16_t offset)
{
	return read_string(conn, attr, buf, len, offset,
			   _ble_app_cb.device_name);
}

ssize_t on_gap_rd_appearance(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len,
			     uint16_t offset)
{
	uint16_t appearance = BLE_APP_APPEARANCE;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &appearance,
				 sizeof(appearance));
}

ssize_t on_gap_rd_ppcp(struct bt_conn *conn,
		       const struct bt_gatt_attr *attr,
		       void *buf, uint16_t len,
		       uint16_t offset)
{
	/* Default values */
	struct gap_ppcp ppcp;

	ppcp.interval_min = sys_cpu_to_le16(MIN_CONN_INTERVAL);
	ppcp.interval_max = sys_cpu_to_le16(MAX_CONN_INTERVAL);
	ppcp.slave_latency = sys_cpu_to_le16(SLAVE_LATENCY);
	ppcp.link_sup_to = sys_cpu_to_le16(CONN_SUP_TIMEOUT);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &ppcp,
				 sizeof(ppcp));
}

ssize_t on_dis_rd_manufacturer(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return read_string(conn, attr, buf, len, offset, MANUFACTURER_NAME);
}

ssize_t on_dis_rd_model(struct bt_conn *conn,
			const struct bt_gatt_attr *attr, void *buf,
			uint16_t len, uint16_t offset)
{
	return read_string(conn, attr, buf, len, offset, MODEL_NUM);
}

ssize_t on_dis_rd_serial(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	uint8_t sn[32];
	uint8_t sn_len;

	/* Check if the factory data contains a serial number otherwise return BD Address */
	if (!memcmp(global_factory_data->oem_data.magic, FACTORY_DATA_MAGIC,
		    4)) {
		struct oem_data *p_oem =
			(struct oem_data *)&global_factory_data->oem_data;
		uint8buf_to_ascii(sn, p_oem->uuid, sizeof(p_oem->uuid));
		sn_len = sizeof(sn);
	} else {
		uint8buf_to_ascii(sn, _ble_app_cb.my_bd_addr.val, 6);
		sn_len = 12;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, sn, sn_len);
}

ssize_t on_dis_rd_hw_rev(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	return read_string(conn, attr, buf, len, offset, HARDWARE_REV);
}

ssize_t on_dis_rd_fw_rev(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	char value[21];

	snprintf(value, 21,
		 "%d.%d.%d", version_header.major,
		 version_header.minor, version_header.patch);
	value[20] = '\0';

	return read_string(conn, attr, buf, len, offset, value);
}

ssize_t on_dis_rd_sw_rev(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	char value[21];

	snprintf(value, 21, "%.20s", version_header.version_string);

	return read_string(conn, attr, buf, len, offset, value);
}

static void on_storage_ble_name_read(void *p_name, uint16_t len, intptr_t priv)
{
	struct ble_enable_config en_config = { 0, };
	const struct bt_le_conn_param conn_params = {
		MIN_CONN_INTERVAL, MAX_CONN_INTERVAL, SLAVE_LATENCY,
		CONN_SUP_TIMEOUT
	};
	bt_addr_le_t bda;

	en_config.central_conn_params = conn_params;

	/* Check if there is a factory partition */
	if (!memcmp(global_factory_data->oem_data.magic, FACTORY_DATA_MAGIC,
		    4)) {
		struct curie_oem_data *p_oem =
			(struct curie_oem_data *)&global_factory_data->oem_data
			.project_data;
		if (p_oem->bt_mac_address_type < 2) {
			bda.type = p_oem->bt_mac_address_type;
			/* copy address in LSB order */
			for (int i = 0; i < sizeof(bda.val); i++) {
				bda.val[i] =
					p_oem->bt_address[sizeof(bda.val) - 1 -
							  i];
			}
			en_config.p_bda = &bda;
		}

		/* If the properties contained no name and the factory does */
		if (!p_name && p_oem->ble_name[0] != 0xFF) {
			p_name = p_oem->ble_name;
		}
	}
	ble_app_set_device_name(p_name, false);

	if (E_OS_OK != ble_service_enable(_ble_app_cb.p_ble_conn, 1, &en_config,
					  &_ble_app_cb)) {
		pr_error(LOG_MODULE_BLE, "BLE ENABLE FAILED");
		return;
	}
}

static void ble_app_delete_conn_timer(void)
{
	/* Destroy the timer */
	if (_ble_app_cb.conn_timer)
		timer_delete(_ble_app_cb.conn_timer);

	/* Make sure that the reference to the timer is lost */
	_ble_app_cb.conn_timer = NULL;
}

static void conn_params_timer_handler(void *privData)
{
	OS_ERR_TYPE os_err;
	struct bt_conn_info info = { 0 };
	const struct bt_le_conn_param conn_params = {
		MIN_CONN_INTERVAL, MAX_CONN_INTERVAL, SLAVE_LATENCY,
		CONN_SUP_TIMEOUT
	};

	bt_conn_get_info(_ble_app_cb.conn_periph, &info);

	/* Check if there was an update in the connection parameters */
	if (info.role != BT_CONN_ROLE_MASTER &&
	    (_ble_app_cb.conn_values.latency != SLAVE_LATENCY ||
	     _ble_app_cb.conn_values.supervision_to != CONN_SUP_TIMEOUT ||
	     _ble_app_cb.conn_values.interval < MIN_CONN_INTERVAL ||
	     _ble_app_cb.conn_values.interval > MAX_CONN_INTERVAL)) {
		/* Start the 30 seconds timer to retry updating the connection */
		timer_start(_ble_app_cb.conn_timer, 30000, &os_err);

		/* Send request to update the connection */
		bt_conn_le_param_update(_ble_app_cb.conn_periph, &conn_params);
	} else
		ble_app_delete_conn_timer();
}

#ifdef CONFIG_BLE_APP_USE_BAT
static void on_batt_service_open(cfw_service_conn_t *p_conn, void *param)
{
	int client_events[] = { MSG_ID_BATTERY_SERVICE_LEVEL_UPDATED_EVT };

	if (_ble_app_cb.p_batt_conn) {
		pr_warning(LOG_MODULE_BLE, "BATT twice?");
		return;
	}
	_ble_app_cb.p_batt_conn = p_conn;

	cfw_register_events(_ble_app_cb.p_batt_conn, client_events,
			    sizeof(client_events) / sizeof(int), &_ble_app_cb);

	battery_service_get_info(_ble_app_cb.p_batt_conn, BATTERY_DATA_LEVEL,
				 NULL);
}
#endif

static void on_ble_service_open(cfw_service_conn_t *p_conn, void *param)
{
	if (_ble_app_cb.p_ble_conn) {
		pr_warning(LOG_MODULE_BLE, "BLE twice?");
		return;
	}
	_ble_app_cb.p_ble_conn = p_conn;
	_ble_app_cb.conn_periph = NULL;
	_ble_app_cb.flags = 0;

	/* read device name, will be used at BLE enable */
	ble_app_prop_read(BLE_PROPERTY_ID_DEVICE_NAME, on_storage_ble_name_read,
			  0);

	/* List of events to receive */
	int client_events[] = {
		MSG_ID_BLE_ADV_TO_EVT,
	};
	cfw_register_events(_ble_app_cb.p_ble_conn, client_events,
			    sizeof(client_events) / sizeof(int), &_ble_app_cb);
}

static void on_properties_service_open(cfw_service_conn_t *p_conn, void *param)
{
	/* The client reference was passed as parameter */
	cfw_client_t *client = param;

	/* Check if the properties connection is already initialized */
	if (_ble_app_cb.p_props_conn) {
		pr_warning(LOG_MODULE_BLE, "ble_app: properties twice?");
		return;
	}
	_ble_app_cb.p_props_conn = p_conn;

	/* start BLE app when BLE_SERVICE is available */
	cfw_open_service_helper(client, BLE_SERVICE_ID,
				on_ble_service_open, &_ble_app_cb);

#ifdef CONFIG_BLE_APP_USE_BAT
	/* add the battery service helper from the beginning */
	cfw_open_service_helper(client, BATTERY_SERVICE_ID,
				on_batt_service_open, &_ble_app_cb);
#endif
}

void ble_app_adv_timer_delete(void)
{
	if (_ble_app_cb.adv_timer != NULL) {
		/* Destroy the timer */
		timer_delete(_ble_app_cb.adv_timer);

		/* Make sure that the reference to the timer is lost */
		_ble_app_cb.adv_timer = NULL;
	}
}

static void adv_timeout_cb(void)
{
	/* when advertisement timedout, start slow advertising without timeout */
	ble_app_start_advertisement(BLE_ADV_TIMEOUT);
}

static void adv_timer_handler(void *privData)
{
	ble_adv_timeout_cb_t adv_cb = privData;

	/* stop & clean up timer */
	ble_app_adv_timer_delete();

	bt_le_adv_stop();

	if (adv_cb)
		adv_cb();
}

void ble_app_adv_timer_start(uint16_t timeout, ble_adv_timeout_cb_t cb)
{
	/* Start a timer to configure the parameters */
	_ble_app_cb.adv_timeout = timeout * 1000;
	_ble_app_cb.adv_timer = timer_create(adv_timer_handler, cb,
					     _ble_app_cb.adv_timeout, false,
					     true,
					     NULL);
}

static void advertise_start(enum ble_adv_reason reason)
{
	size_t adv_len;
	struct bt_le_adv_param adv_param;
	uint16_t interval;
	uint16_t timeout;

	struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS,
			      (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
		BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, BLE_APP_APPEARANCE & 0xFF,
			      BLE_APP_APPEARANCE >> 8),
		BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA, BLE_APP_MANUFACTURER &
			      0xFF,
			      BLE_APP_MANUFACTURER >> 8),
		BT_DATA(BT_DATA_NAME_COMPLETE, _ble_app_cb.device_name,
			strlen((char *)_ble_app_cb.device_name))
	};

	struct bt_data *ad_name = &ad[3];
	int status;

	pr_info(LOG_MODULE_MAIN, "advertise_start: reason:0x%x", reason);

	switch (reason) {
	case BLE_ADV_USER1:
	case BLE_ADV_USER2:
	case BLE_ADV_DISCONNECT:
		timeout = CONFIG_BLE_APP_DEFAULT_ADV_TIMEOUT;
		interval = CONFIG_BLE_APP_DEFAULT_ADV_INTERVAL;
		break;
	case BLE_ADV_TIMEOUT:
		interval = CONFIG_BLE_ADV_SLOW_INTERVAL;
		timeout = CONFIG_BLE_ADV_SLOW_TIMEOUT;
		break;
	case BLE_ADV_STARTUP:
		interval = CONFIG_BLE_ULTRA_FAST_ADV_INTERVAL;
		if (get_boot_target() == TARGET_RECOVERY)
			timeout = 0;
		else
			timeout = CONFIG_BLE_APP_DEFAULT_ADV_TIMEOUT;
		break;
	default:
		interval = CONFIG_BLE_APP_DEFAULT_ADV_INTERVAL;
		timeout = 0;
		break;
	}

	adv_param.type = BT_LE_ADV_IND;
	adv_param.interval_max = interval;
	adv_param.interval_min = interval;
	adv_param.addr_type = _ble_app_cb.my_bd_addr.type;

	adv_len = adv_data_len(ad, ARRAY_SIZE(ad)) + 2 * ARRAY_SIZE(ad);
	if (adv_len > 31) {
		ad_name->data_len -= adv_len - 31;
		ad_name->type = BT_DATA_NAME_SHORTENED;
	}

	status = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);

	if (!status && timeout)
		ble_app_adv_timer_start(timeout, adv_timeout_cb);

	if (status)
		pr_error(LOG_MODULE_MAIN, "start_adv err %d", status);
}

static void get_bonded_dev_cb(const struct nble_gap_sm_bond_info *info,
			      const bt_addr_le_t *peer_addr,
			      uint16_t len, void *user_data)
{
	uint8_t is_bonded;
	int reason = (int)user_data;

	is_bonded = (info->addr_count + info->irk_count) ? 1 : 0;
	if (is_bonded) {
		pr_info(LOG_MODULE_MAIN, "Device bonded");
	} else {
		pr_info(LOG_MODULE_MAIN, "Device not bonded");
	}

	advertise_start(reason);
}

__weak void ble_app_start_advertisement(enum ble_adv_reason reason)
{
	/* retrieve number of bonded devices */
	ble_gap_get_bonding_info(get_bonded_dev_cb, (void *)reason, false);
}

void ble_app_stop_advertisement(void)
{
	ble_app_adv_timer_delete();
	bt_le_adv_stop();
}

__weak void on_ble_app_started(void)
{
}

static void _ble_register_services(void)
{
	/* GAP_SVC */
	ble_gap_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "GAP");

	/* DIS_SVC */
	ble_dis_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "DIS");

	/* BAS_SVC */
	ble_bas_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "BAS");

#ifdef CONFIG_BLE_HRS_LIB
	/* HRM_SVC */
	ble_hrs_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "HRM");
#endif

#ifdef CONFIG_BLE_LNS_LIB
	/* LNS_SVC */
	ble_lns_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "LNS");
#endif

#ifdef CONFIG_BLE_RSCS_LIB
	/* RSC_SVC */
	ble_rscs_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "RSC");
#endif

#if defined(CONFIG_PACKAGE_ISPP)
	/* ISPP_SVC */
	ble_ispp_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "ISPP");
#endif
#if defined(CONFIG_UAS)
	ble_uas_init();
	pr_info(LOG_MODULE_BLE, "Registering %s", "UAS");
#endif

	on_ble_app_started();
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info = { 0 };

	bt_conn_get_info(conn, &info);

	if (!err) {
		if (info.role == BT_CONN_ROLE_SLAVE) {
			bt_conn_ref(conn);
			_ble_app_cb.conn_periph = conn;

			/* stop adv timer */
			ble_app_stop_advertisement();
			_ble_app_cb.conn_values.interval = info.le.interval;
			_ble_app_cb.conn_values.latency = info.le.latency;
			_ble_app_cb.conn_values.supervision_to =
				info.le.timeout;

			/* If peripheral and connection values are not compliant with the PPCP */
			/* Start a timer to configure the parameters */
			_ble_app_cb.conn_timer = timer_create(
				conn_params_timer_handler,
				NULL, 5000, false,
				true, NULL);
		}

#if !defined(BLE_APP_DEBUG)
		pr_info(LOG_MODULE_MAIN, "BLE connected (conn: %p, role: %d)",
			conn,
			info.role);
#else
		pr_info(
			LOG_MODULE_MAIN,
			"BLE connected (conn: %p, role: %d) to "
			"%02x:%02x:%02x:%02x:%02x:%02x/%d", conn, info.role,
			info.le.src->val[5], info.le.src->val[4],
			info.le.src->val[3], info.le.src->val[2],
			info.le.src->val[1], info.le.src->val[0],
			info.type);
#endif
#ifdef CONFIG_SYSTEM_EVENTS
		system_event_push_ble_conn(true,
					   (uint8_t *)&info.le.src->val[0]);
#endif
	} else {
		pr_info(LOG_MODULE_MAIN, "BLE connection KO(conn: %p)", conn);
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct bt_conn_info info;

	bt_conn_get_info(conn, &info);

	if (info.role == BT_CONN_ROLE_SLAVE) {
		_ble_app_cb.conn_periph = NULL;
		bt_conn_unref(conn);

		ble_app_stop_advertisement();
		ble_app_start_advertisement(BLE_ADV_DISCONNECT);
	}
#ifdef CONFIG_SYSTEM_EVENTS
	system_event_push_ble_conn(false, NULL);
#endif
	pr_info(LOG_MODULE_MAIN, "BLE disconnected(conn: %p, hci_reason: 0x%x)",
		conn, reason);

	ble_app_delete_conn_timer();
}

static void on_le_param_updated(struct bt_conn *conn, uint16_t interval,
				uint16_t latency, uint16_t timeout)
{
	_ble_app_cb.conn_values.interval = interval;
	_ble_app_cb.conn_values.latency = latency;
	_ble_app_cb.conn_values.supervision_to = timeout;
}

static struct bt_conn_cb conn_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.le_param_updated = on_le_param_updated
};

/* Handles BLE enable message */
static void handle_msg_id_ble_enable_rsp(struct cfw_message *msg)
{
	struct ble_enable_rsp *rsp = container_of(msg, struct ble_enable_rsp,
						  header);

	if (!rsp->status) {
		_ble_app_cb.flags |= BLE_APP_ENABLED;
		_ble_app_cb.my_bd_addr = rsp->bd_addr;

		/* If the name is still not configured, build one */
		if (!_ble_app_cb.device_name[0]) {
			snprintf(_ble_app_cb.device_name,
				 sizeof(_ble_app_cb.device_name),
				 CONFIG_BLE_DEV_NAME "%02x%02x%02x%02x%02x%02x",
				 rsp->bd_addr.val[5], rsp->bd_addr.val[4],
				 rsp->bd_addr.val[3],
				 rsp->bd_addr.val[2], rsp->bd_addr.val[1],
				 rsp->bd_addr.val[0]);
		}

		bt_conn_cb_register(&conn_callbacks);
#ifdef CONFIG_TCMD_BLE
		ble_tcmd_init();
#endif

#if CONFIG_BLE_TX_POWER != 0
		ble_gap_set_tx_power(CONFIG_BLE_TX_POWER);
#endif

		/* registers all services */
		_ble_register_services();

		pr_info(LOG_MODULE_BLE, "ble_enable_rsp: addr/type: "
			"%.2x:%.2x:%.2x:%.2x:%.2x:%.2x/%d",
			rsp->bd_addr.val[5], rsp->bd_addr.val[4],
			rsp->bd_addr.val[3], rsp->bd_addr.val[2],
			rsp->bd_addr.val[1], rsp->bd_addr.val[0],
			rsp->bd_addr.type);

		ble_app_start_advertisement(BLE_ADV_STARTUP);

#ifdef CONFIG_BLE_APP_USE_BAT
		if (_ble_app_cb.p_batt_conn)
			battery_service_get_info(_ble_app_cb.p_batt_conn,
						 BATTERY_DATA_LEVEL, NULL);
#endif
	} else {
		pr_error(LOG_MODULE_BLE, "enable_rsp err %d", rsp->status);
	}
}

static void handle_ble_property_read_rsp(struct cfw_message *cfw)
{
	properties_service_read_rsp_msg_t *rsp = container_of(
		cfw, properties_service_read_rsp_msg_t, header);
	struct ble_app_prop_rd_msg *msg = CFW_MESSAGE_PRIV(cfw);

	assert(msg);

	uint8_t *data = NULL;
	if (rsp->status == DRV_RC_OK)
		data = rsp->start_of_values;

	msg->cb(data, rsp->property_size, msg->priv);
	bfree(msg);
}

static void ble_app_msg_handler(struct cfw_message *msg, void *param)
{
	switch (CFW_MESSAGE_ID(msg)) {
	case MSG_ID_BLE_ENABLE_RSP:
		handle_msg_id_ble_enable_rsp(msg);
		break;
	case MSG_ID_PROP_SERVICE_ADD_RSP:
		break;
	case MSG_ID_PROP_SERVICE_READ_RSP:
		handle_ble_property_read_rsp(msg);
		break;
	case MSG_ID_PROP_SERVICE_WRITE_RSP:
		break;
#ifdef CONFIG_BLE_APP_USE_BAT
	case MSG_ID_BATTERY_SERVICE_LEVEL_UPDATED_EVT: {
		battery_service_listen_evt_rsp_msg_t *evt = container_of(
			msg, battery_service_listen_evt_rsp_msg_t, header);
		if (_ble_app_cb.flags & BLE_APP_ENABLED)
			ble_bas_update(
				evt->battery_service_evt_content_rsp_msg.
				bat_soc);
		break;
	}
	case MSG_ID_BATTERY_SERVICE_GET_BATTERY_INFO_RSP: {
		struct battery_service_info_request_rsp_msg *batt_msg =
			container_of(
				msg,
				struct battery_service_info_request_rsp_msg,
				header);
		if (_ble_app_cb.flags & BLE_APP_ENABLED)
			ble_bas_update(batt_msg->battery_soc.bat_soc);
		break;
	}
#endif
	default:
		pr_debug(LOG_MODULE_BLE, "Unhandled BLE app message");
	}
	cfw_msg_free(msg);
}

void ble_start_app(T_QUEUE queue)
{
	cfw_client_t *client;

	/* Get a client handle */
	client = cfw_client_init(queue, ble_app_msg_handler, &_ble_app_cb);
	assert(client);

	/* Open properties service -> will resume app initialization */
	cfw_open_service_helper(client, PROPERTIES_SERVICE_ID,
				on_properties_service_open, client);
}

void ble_app_clear_bonds(void)
{
	bt_conn_remove_info(BT_ADDR_LE_ANY);

	pr_info(LOG_MODULE_MAIN, "Pairing cleared");
}

const bt_addr_le_t *ble_app_get_device_bda(void)
{
	return &_ble_app_cb.my_bd_addr;
}

const uint8_t *ble_app_get_device_name(void)
{
	return _ble_app_cb.device_name;
}

int ble_app_set_device_name(const uint8_t *p_device_name, bool store)
{
	struct nble_gap_service_write_params params = { 0 };
	size_t len;

	if (!p_device_name)
		return -EINVAL;

	memcpy(_ble_app_cb.device_name, p_device_name,
	       sizeof(_ble_app_cb.device_name) - 1);
	_ble_app_cb.device_name[sizeof(_ble_app_cb.device_name) - 1] = 0;

	/* the length can not be greater than BLE_MAX_DEVICE_NAME */
	len = strlen(_ble_app_cb.device_name);

	if (store)
		/* Store the new name permanently in the properties: add the entry and write it */
		ble_app_prop_write(BLE_PROPERTY_ID_DEVICE_NAME,
				   _ble_app_cb.device_name, len + 1,
				   BLE_APP_PROP_ADD | BLE_APP_PROP_WRITE);

	if ((_ble_app_cb.flags & BLE_APP_ENABLED)) {
		/* This is a Nordic specific behavior : updating the device
		 * name is not sufficient because GAP attributes access does
		 * not generate an access
		 */
		BUILD_BUG_ON(sizeof(params.name.name_array) !=
			     (sizeof(_ble_app_cb.device_name) - 1));
		memcpy(params.name.name_array, _ble_app_cb.device_name, len);
		params.attr_type = NBLE_GAP_SVC_ATTR_NAME;
		params.name.authorization = 0;
		params.name.len = len;
		params.name.sec_mode = BLE_DEVICE_NAME_WRITE_PERM;
		nble_gap_service_write_req(&params);
	}
	return 0;
}

int ble_app_restore_default_conn(void)
{
	const struct bt_le_conn_param conn_params = {
		MIN_CONN_INTERVAL, MAX_CONN_INTERVAL, SLAVE_LATENCY,
		CONN_SUP_TIMEOUT
	};

	/* Send request to restore default connection */
	return ble_app_conn_update(&conn_params);
}
