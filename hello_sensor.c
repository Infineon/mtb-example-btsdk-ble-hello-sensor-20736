/*
 * Copyright 2016-2024, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 */

/** @file
*
* Bluetooth LE Vendor Specific Device
*
* During initialization the app registers with LE stack to receive various
* notifications including bonding complete, connection status change and
* peer write.  When device is successfully bonded, application saves
* peer's Bluetooth Device address to the NVRAM.  Bonded device can also
* write in to client configuration descriptor of the notification
* characteristic.  That is also save in the NVRAM.  When user pushes the
* button notification is sent to the bonded and registered host.
*
* Features demonstrated
*  - GATT database and Device configuration initialization
*  - Registration with LE stack for various events
*  - NVRAM read/write operation
*  - Sending data to the client
*  - Processing write requests from the client
*  - Use of LED
*
* To demonstrate the app, work through the following steps.
* 1. Plug the WICED eval board into your computer
* 2. Build and download the application (to the WICED board)
* 3. Pair with a client
* 4. On the client side register for notifications
* 5. Push the user button on the board to send notifications to the client
*
*/
#include "spar_utils.h"
#include "bleprofile.h"
#include "bleapp.h"
#include "gpiodriver.h"
#include "string.h"
#include "stdio.h"
#include "stdint.h"
#include "platform.h"
#include "hello_sensor.h"
#include "sparcommon.h"


#define LINK_ENCRYPTION_REQUIRED
extern void blecm_setTxPowerInADV(int);


/******************************************************
 *                      Constants
 ******************************************************/
#define NVRAM_ID_HOST_LIST                    0x10    // ID of the memory block used for NVRAM access

// Please note that all UUIDs need to be reversed when publishing in the database

// {1B7E8251-2877-41C3-B46E-CF057C562023}
// static const GUID UUID_HELLO_SERVICE               = { 0x1b7e8251, 0x2877, 0x41c3, { 0xb4, 0x6e, 0xcf, 0x5, 0x7c, 0x56, 0x20, 0x23 } };
#define UUID_HELLO_SERVICE                  0x23, 0x20, 0x56, 0x7c, 0x05, 0xcf, 0x6e, 0xb4, 0xc3, 0x41, 0x77, 0x28, 0x51, 0x82, 0x7e, 0x1b

// {5E9BF2A8-F93F-4481-A67E-3B2F4A07891A}
// static const GUID UUID_HELLO_CHARACTERISTIC_CONFIG = { 0x5e9bf2a8, 0xf93f, 0x4481, { 0xa6, 0x7e, 0x3b, 0x2f, 0x4a, 0x7, 0x89, 0x1a } };
#define UUID_HELLO_CHARACTERISTIC_CONFIG    0x1a, 0x89, 0x07, 0x4a, 0x2f, 0x3b, 0x7e, 0xa6, 0x81, 0x44, 0x3f, 0xf9, 0xa8, 0xf2, 0x9b, 0x5e

// {8AC32D3F-5CB9-4D44-BEC2-EE689169F626}
// static const GUID UUID_HELLO_CHARACTERISTIC_NOTIFY = { 0x8ac32d3f, 0x5cb9, 0x4d44, { 0xbe, 0xc2, 0xee, 0x68, 0x91, 0x69, 0xf6, 0x26 } };
#define UUID_HELLO_CHARACTERISTIC_NOTIFY    0x26, 0xf6, 0x69, 0x91, 0x68, 0xee, 0xc2, 0xbe, 0x44, 0x4d, 0xb9, 0x5c, 0x3f, 0x2d, 0xc3, 0x8a


/******************************************************
 *                     Structures
 ******************************************************/

#pragma pack(1)
//host information for NVRAM
typedef PACKED struct
{
    // BD address of the bonded host
    BD_ADDR  bdaddr;

    // Current value of the client configuration descriptor
    UINT16  characteristic_client_configuration;

    // sensor configuration. number of times to blink when button is pushed.
    UINT8   number_of_blinks;

}  HOSTINFO;
#pragma pack()

/******************************************************
 *               Function Prototypes
 ******************************************************/

static void hello_sensor_create(void);
static void hello_sensor_timeout( UINT32 count );
static void hello_sensor_fine_timeout( UINT32 finecount );
static void hello_sensor_database_init( void );
static void hello_sensor_connection_up( void );
static void hello_sensor_connection_down( void );
static void hello_sensor_advertisement_stopped( void );
static void hello_sensor_smp_bond_result( LESMP_PARING_RESULT result );
static void hello_sensor_encryption_changed( HCI_EVT_HDR *evt );
static void hello_sensor_send_message( void );
static int  hello_sensor_write_handler( LEGATTDB_ENTRY_HDR *p );
static void hello_sensor_indication_cfm( void );
static void hello_sensor_interrupt_handler( UINT8 value );
extern void bleprofile_appTimerCb( UINT32 arg );

/******************************************************
 *               Variables Definitions
 ******************************************************/

/*
 * This is the GATT database for the Hello Sensor application.  It defines
 * services, characteristics and descriptors supported by the sensor.  Each
 * attribute in the database has a handle, (characteristic has two, one for
 * characteristic itself, another for the value).  The handles are used by
 * the peer to access attributes, and can be used locally by application for
 * example to retrieve data written by the peer.  Definition of characteristics
 * and descriptors has GATT Properties (read, write, notify...) but also has
 * permissions which identify if application is allowed to read or write
 * into it.  Handles do not need to be sequential, but need to be in order.
 */
const UINT8 hello_sensor_gatt_database[]=
{
    // Handle 0x01: GATT service
    // Service change characteristic is optional and is not present
    PRIMARY_SERVICE_UUID16 (0x0001, UUID_SERVICE_GATT),

    // Handle 0x14: GAP service
    // Device Name and Appearance are mandatory characteristics.  Peripheral
    // Privacy Flag only required if privacy feature is supported.  Reconnection
    // Address is optional and only when privacy feature is supported.
    // Peripheral Preferred Connection Parameters characteristic is optional
    // and not present.
    PRIMARY_SERVICE_UUID16 (0x0014, UUID_SERVICE_GAP),

    // Handle 0x15: characteristic Device Name, handle 0x16 characteristic value.
    // Any 16 byte string can be used to identify the sensor.  Just need to
    // replace the "Hello" string below.  Keep it short so that it fits in
    // advertisement data along with 16 byte UUID.
    CHARACTERISTIC_UUID16 (0x0015, 0x0016, UUID_CHARACTERISTIC_DEVICE_NAME,
                       LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE, 16),
       'H','e','l','l','o',0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

    // Handle 0x17: characteristic Appearance, handle 0x18 characteristic value.
    // List of approved appearances is available at bluetooth.org.  Current
    // value is set to 0x200 - Generic Tag
    CHARACTERISTIC_UUID16 (0x0017, 0x0018, UUID_CHARACTERISTIC_APPEARANCE,
                       LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE, 2),
        BIT16_TO_8(APPEARANCE_GENERIC_TAG),

    // Handle 0x28: Hello Service.
    // This is the main proprietary service of Hello Sensor.  It has 2 characteristics.
    // One will be used to send notification(s) to the paired client when button is
    // pushed, another is a configuration of the device.  The only thing which
    // can be configured is number of times to send notification.  Note that
    // UUID of the vendor specific service is 16 bytes, unlike standard Bluetooth
    // UUIDs which are 2 bytes.  _UUID128 version of the macro should be used.
    PRIMARY_SERVICE_UUID128 (HANDLE_HELLO_SENSOR_SERVICE_UUID, UUID_HELLO_SERVICE),

    // Handle 0x29: characteristic Hello Notification, handle 0x2a characteristic value
    // we support both notification and indication.  Peer need to allow notifications
    // or indications by writing in the Characteristic Client Configuration Descriptor
    // (see handle 2b below).  Note that UUID of the vendor specific characteristic is
    // 16 bytes, unlike standard Bluetooth UUIDs which are 2 bytes.  _UUID128 version
    // of the macro should be used.
    CHARACTERISTIC_UUID128 (0x0029, HANDLE_HELLO_SENSOR_VALUE_NOTIFY, UUID_HELLO_CHARACTERISTIC_NOTIFY,
                           LEGATTDB_CHAR_PROP_READ | LEGATTDB_CHAR_PROP_NOTIFY | LEGATTDB_CHAR_PROP_INDICATE,
#ifdef LINK_ENCRYPTION_REQUIRED
                           LEGATTDB_PERM_READABLE | LEGATTDB_PERM_AUTH_READABLE,
#else
                           LEGATTDB_PERM_READABLE,
#endif
                           7),
        'H','e','l','l','o',' ','0',

    // Handle 0x2b: Characteristic Client Configuration Descriptor.
    // This is standard GATT characteristic descriptor.  2 byte value 0 means that
    // message to the client is disabled.  Peer can write value 1 or 2 to enable
    // notifications or indications respectively.  Not _WRITABLE in the macro.  This
    // means that attribute can be written by the peer.
    CHAR_DESCRIPTOR_UUID16_WRITABLE (HANDLE_HELLO_SENSOR_CLIENT_CONFIGURATION_DESCRIPTOR,
                                     UUID_DESCRIPTOR_CLIENT_CHARACTERISTIC_CONFIGURATION,
#ifdef LINK_ENCRYPTION_REQUIRED
                                     LEGATTDB_PERM_READABLE | LEGATTDB_PERM_AUTH_READABLE | LEGATTDB_PERM_WRITE_REQ | LEGATTDB_PERM_AUTH_WRITABLE,
#else
                                     LEGATTDB_PERM_READABLE | LEGATTDB_PERM_WRITE_REQ,
#endif
                                     2),
        0x00,0x00,

    // Handle 0x2c: characteristic Hello Configuration, handle 0x2d characteristic value
    // The configuration consists of 1 bytes which indicates how many notifications or
    // indications to send when user pushes the button.
    CHARACTERISTIC_UUID128_WRITABLE (0x002c, HANDLE_HELLO_SENSOR_CONFIGURATION, UUID_HELLO_CHARACTERISTIC_CONFIG,
                                     LEGATTDB_CHAR_PROP_READ | LEGATTDB_CHAR_PROP_WRITE,
#ifdef LINK_ENCRYPTION_REQUIRED
                                     LEGATTDB_PERM_READABLE | LEGATTDB_PERM_AUTH_READABLE | LEGATTDB_PERM_WRITE_CMD | LEGATTDB_PERM_WRITE_REQ | LEGATTDB_PERM_AUTH_WRITABLE,  1),
#else
                                     LEGATTDB_PERM_READABLE | LEGATTDB_PERM_WRITE_CMD | LEGATTDB_PERM_WRITE_REQ,  1),
#endif
        0x00,

    // Handle 0x4d: Device Info service
    // Device Information service helps peer to identify manufacture or vendor
    // of the device.  It is required for some types of the devices (for example HID,
    // and medical, and optional for others.  There are a bunch of characteristics
    // available, out of which Hello Sensor implements 3.
    PRIMARY_SERVICE_UUID16 (0x004d, UUID_SERVICE_DEVICE_INFORMATION),

    // Handle 0x4e: characteristic Manufacturer Name, handle 0x4f characteristic value
    CHARACTERISTIC_UUID16 (0x004e, 0x004f, UUID_CHARACTERISTIC_MANUFACTURER_NAME_STRING, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE, 8),
        'I','n','f','i','n','e','o','n',

    // Handle 0x50: characteristic Model Number, handle 0x51 characteristic value
    CHARACTERISTIC_UUID16 (0x0050, 0x0051, UUID_CHARACTERISTIC_MODEL_NUMBER_STRING, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE, 8),
        '1','2','3','4',0x00,0x00,0x00,0x00,

    // Handle 0x52: characteristic System ID, handle 0x53 characteristic value
    CHARACTERISTIC_UUID16 (0x0052, 0x0053, UUID_CHARACTERISTIC_SYSTEM_ID, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE, 8),
        0x93,0xb8,0x63,0x80,0x5f,0x9f,0x91,0x71,

    // Handle 0x61: Battery service
    // This is an optional service which allows peer to read current battery level.
    PRIMARY_SERVICE_UUID16 (0x0061, UUID_SERVICE_BATTERY),

    // Handle 0x62: characteristic Battery Level, handle 0x63 characteristic value
    CHARACTERISTIC_UUID16 (0x0062, 0x0063, UUID_CHARACTERISTIC_BATTERY_LEVEL,
                           LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE, 1),
        0x64,
};

const BLE_PROFILE_CFG hello_sensor_cfg =
{
    /*.fine_timer_interval            =*/ 200,  // ms
    /*.default_adv                    =*/ 4,    // HIGH_UNDIRECTED_DISCOVERABLE
    /*.button_adv_toggle              =*/ 0,    // pairing button make adv toggle (if 1) or always on (if 0)
    /*.high_undirect_adv_interval     =*/ 32,   // slots
    /*.low_undirect_adv_interval      =*/ 1024, // slots
    /*.high_undirect_adv_duration     =*/ 30,   // seconds
    /*.low_undirect_adv_duration      =*/ 300,  // seconds
    /*.high_direct_adv_interval       =*/ 0,    // seconds
    /*.low_direct_adv_interval        =*/ 0,    // seconds
    /*.high_direct_adv_duration       =*/ 0,    // seconds
    /*.low_direct_adv_duration        =*/ 0,    // seconds
    /*.local_name                     =*/ "Hello",        // [LOCAL_NAME_LEN_MAX];
    /*.cod                            =*/ {BIT16_TO_8(APPEARANCE_GENERIC_TAG),0x00}, // [COD_LEN];
    /*.ver                            =*/ "1.00",         // [VERSION_LEN];
#ifdef LINK_ENCRYPTION_REQUIRED
    /*.encr_required                  =*/ (SECURITY_ENABLED | SECURITY_REQUEST),    // data encrypted and device sends security request on every connection
#else
    /*.encr_required                  =*/ 0,    // data encrypted and device sends security request on every connection
#endif
    /*.disc_required                  =*/ 0,    // if 1, disconnection after confirmation
    /*.test_enable                    =*/ 1,    // TEST MODE is enabled when 1
    /*.tx_power_level                 =*/ 0x04, // dbm
    /*.con_idle_timeout               =*/ 3,    // second  0-> no timeout
    /*.powersave_timeout              =*/ 0,    // second  0-> no timeout
    /*.hdl                            =*/ {0x00, 0x0063, 0x00, 0x00, 0x00}, // [HANDLE_NUM_MAX];
    /*.serv                           =*/ {0x00, UUID_SERVICE_BATTERY, 0x00, 0x00, 0x00},
    /*.cha                            =*/ {0x00, UUID_CHARACTERISTIC_BATTERY_LEVEL, 0x00, 0x00, 0x00},
    /*.findme_locator_enable          =*/ 0,    // if 1 Find me locator is enable
    /*.findme_alert_level             =*/ 0,    // alert level of find me
    /*.client_grouptype_enable        =*/ 0,    // if 1 grouptype read can be used
    /*.linkloss_button_enable         =*/ 0,    // if 1 linkloss button is enable
    /*.pathloss_check_interval        =*/ 0,    // second
    /*.alert_interval                 =*/ 0,    // interval of alert
    /*.high_alert_num                 =*/ 0,    // number of alert for each interval
    /*.mild_alert_num                 =*/ 0,    // number of alert for each interval
    /*.status_led_enable              =*/ 1,    // if 1 status LED is enable
    /*.status_led_interval            =*/ 0,    // second
    /*.status_led_con_blink           =*/ 0,    // blink num of connection
    /*.status_led_dir_adv_blink       =*/ 0,    // blink num of dir adv
    /*.status_led_un_adv_blink        =*/ 0,    // blink num of undir adv
    /*.led_on_ms                      =*/ 0,    // led blink on duration in ms
    /*.led_off_ms                     =*/ 0,    // led blink off duration in ms
    /*.buz_on_ms                      =*/ 100,  // buzzer on duration in ms
    /*.button_power_timeout           =*/ 0,    // seconds
    /*.button_client_timeout          =*/ 0,    // seconds
    /*.button_discover_timeout        =*/ 0,    // seconds
    /*.button_filter_timeout          =*/ 0,    // seconds
#ifdef BLE_UART_LOOPBACK_TRACE
    /*.button_uart_timeout            =*/ 15,   // seconds
#endif
};

// Following structure defines UART configuration
const BLE_PROFILE_PUART_CFG hello_sensor_puart_cfg =
{
    /*.baudrate   =*/ 115200,
    /*.txpin      =*/ PUARTENABLE | GPIO_PIN_UART_TX,
    /*.rxpin      =*/ PUARTENABLE | GPIO_PIN_UART_RX,
};

// Following structure defines GPIO configuration used by the application
const BLE_PROFILE_GPIO_CFG hello_sensor_gpio_cfg =
{
    /*.gpio_pin =*/
    {
    GPIO_PIN_WP,      // This need to be used to enable/disable NVRAM write protect
    GPIO_PIN_BUTTON,  // Button GPIO is configured to trigger either direction of interrupt
    GPIO_PIN_LED,     // LED GPIO, optional to provide visual effects
    GPIO_PIN_BATTERY, // Battery monitoring GPIO. When it is lower than particular level, it will give notification to the application
    GPIO_PIN_BUZZER,  // Buzzer GPIO, optional to provide audio effects
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 // other GPIOs are not used
    },
    /*.gpio_flag =*/
    {
    GPIO_SETTINGS_WP,
    GPIO_SETTINGS_BUTTON,
    GPIO_SETTINGS_LED,
    GPIO_SETTINGS_BATTERY,
    GPIO_SETTINGS_BUZZER,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    }
};

UINT32 	hello_sensor_timer_count        = 0;
UINT32 	hello_sensor_fine_timer_count   = 0;
UINT16 	hello_sensor_connection_handle	= 0;    // HCI handle of connection, not zero when connected
BD_ADDR hello_sensor_remote_addr        = {0, 0, 0, 0, 0, 0};
UINT8 	hello_sensor_indication_sent    = 0;    // indication sent, waiting for ack
UINT8   hello_sensor_num_to_write       = 0;  	// Number of messages we need to send
UINT8   hello_sensor_stay_connected	    = 1;    // Change that to 0 to disconnect when all messages are sent

// NVRAM save area
HOSTINFO hello_sensor_hostinfo;

/******************************************************
 *               Function Definitions
 ******************************************************/

// Application initialization
APPLICATION_INIT()
{
    bleapp_set_cfg((UINT8 *)hello_sensor_gatt_database,
                   sizeof(hello_sensor_gatt_database),
                   (void *)&hello_sensor_cfg,
                   (void *)&hello_sensor_puart_cfg,
                   (void *)&hello_sensor_gpio_cfg,
                   hello_sensor_create);

    // BLE_APP_DISABLE_TRACING();     ////// Uncomment to disable all tracing
    BLE_APP_ENABLE_TRACING_ON_PUART();
}

// Create hello sensor
void hello_sensor_create(void)
{
    BLEPROFILE_DB_PDU db_pdu;

    extern UINT32 blecm_configFlag ;
    blecm_configFlag |= BLECM_DBGUART_LOG | BLECM_DBGUART_LOG_L2CAP | BLECM_DBGUART_LOG_SMP;

    ble_trace0("hello_sensor_create()\n");
    ble_trace0(bleprofile_p_cfg->ver);
    ble_trace0("\n");

    // dump the database to debug uart.
    legattdb_dumpDb();

    bleprofile_Init(bleprofile_p_cfg);
    bleprofile_GPIOInit(bleprofile_gpio_p_cfg);

    hello_sensor_database_init();        //load handle number

    // register connection up and connection down handler.
    bleprofile_regAppEvtHandler(BLECM_APP_EVT_LINK_UP, hello_sensor_connection_up);
    bleprofile_regAppEvtHandler(BLECM_APP_EVT_LINK_DOWN, hello_sensor_connection_down);
    bleprofile_regAppEvtHandler(BLECM_APP_EVT_ADV_TIMEOUT, hello_sensor_advertisement_stopped);

    // handler for Encryption changed.
    blecm_regEncryptionChangedHandler(hello_sensor_encryption_changed);

    // handler for Bond result
    lesmp_regSMPResultCb((LESMP_SINGLE_PARAM_CB) hello_sensor_smp_bond_result);

    // register to process client writes
    legattdb_regWriteHandleCb((LEGATTDB_WRITE_CB)hello_sensor_write_handler);

    // register interrupt handler
    bleprofile_regIntCb((BLEPROFILE_SINGLE_PARAM_CB) hello_sensor_interrupt_handler);

    bleprofile_regTimerCb(hello_sensor_fine_timeout, hello_sensor_timeout);
    bleprofile_StartTimer();

    // Read value of the service from GATT DB.
    bleprofile_ReadHandle(HANDLE_HELLO_SENSOR_SERVICE_UUID, &db_pdu);
    ble_tracen((char *)db_pdu.pdu, db_pdu.len);
    ble_trace0("\n");

    if (db_pdu.len != 16)
    {
        ble_trace1("hello_sensor bad service UUID len: %d\n", db_pdu.len);
    }
    else
    {
        // total length should be less than 31 bytes
        BLE_ADV_FIELD adv[3];

        // flags
        adv[0].len     = 1 + 1;
        adv[0].val     = ADV_FLAGS;
        adv[0].data[0] = LE_LIMITED_DISCOVERABLE | BR_EDR_NOT_SUPPORTED;

        adv[1].len     = 16 + 1;
        adv[1].val     = ADV_SERVICE_UUID128_COMP;
        memcpy(adv[1].data, db_pdu.pdu, 16);

        // name
        adv[2].len      = strlen(bleprofile_p_cfg->local_name) + 1;
        adv[2].val      = ADV_LOCAL_NAME_COMP;
        memcpy(adv[2].data, bleprofile_p_cfg->local_name, adv[2].len - 1);

        bleprofile_GenerateADVData(adv, 3);
    }
    blecm_setTxPowerInADV(0);
    bleprofile_Discoverable(HIGH_UNDIRECTED_DISCOVERABLE, hello_sensor_remote_addr);
}

// Initialize GATT database
void hello_sensor_database_init(void)
{
    //Initialized ROM code which will monitor the battery
    blebat_Init();
}

// This function will be called on every connection establishmen
void hello_sensor_connection_up(void)
{
    UINT8 writtenbyte;
    UINT8 *bda;

    hello_sensor_connection_handle = (UINT16)emconinfo_getConnHandle();

    // save address of the connected device and print it out.
    memcpy(hello_sensor_remote_addr, (UINT8 *)emconninfo_getPeerPubAddr(), sizeof(hello_sensor_remote_addr));

    ble_trace5("hello_sensor_connection_up: %08x%04x type %d bonded %d handle %04x\n",
                (hello_sensor_remote_addr[5] << 24) + (hello_sensor_remote_addr[4] << 16) +
                (hello_sensor_remote_addr[3] << 8) + hello_sensor_remote_addr[2],
                (hello_sensor_remote_addr[1] << 8) + hello_sensor_remote_addr[0],
                emconninfo_getPeerAddrType(), emconninfo_deviceBonded(), hello_sensor_connection_handle);

    // Stop advertising
    bleprofile_Discoverable(NO_DISCOVERABLE, NULL);

    bleprofile_StopConnIdleTimer();

    // as we require security for every connection, we will not send any indications until
    // encryption is done.
    if (bleprofile_p_cfg->encr_required != 0)
    {

        lesmp_pinfo->pairingParam.AuthReq  |= LESMP_AUTH_FLAG_BONDING;
        lesmp_sendSecurityRequest();

        return;
    }
    // saving bd_addr in nvram

    bda =(UINT8 *)emconninfo_getPeerPubAddr();

    memcpy(hello_sensor_hostinfo.bdaddr, bda, sizeof(BD_ADDR));
    hello_sensor_hostinfo.characteristic_client_configuration = 0;
    hello_sensor_hostinfo.number_of_blinks = 0;

    writtenbyte = bleprofile_WriteNVRAM(NVRAM_ID_HOST_LIST, sizeof(hello_sensor_hostinfo), (UINT8 *)&hello_sensor_hostinfo);
    ble_trace1("NVRAM write:%04x\n", writtenbyte);

    hello_sensor_encryption_changed(NULL);
}

// This function will be called when connection goes down
void hello_sensor_connection_down(void)
{
    ble_trace1("handle:%d\n", hello_sensor_connection_handle);

    memset (hello_sensor_remote_addr, 0, 6);
    hello_sensor_connection_handle = 0;

    // If we are configured to stay connected, disconnection was caused by the
    // peer, start low advertisements, so that peer can connect when it wakes up.
    if (hello_sensor_stay_connected)
    {
        bleprofile_Discoverable(LOW_UNDIRECTED_DISCOVERABLE, hello_sensor_hostinfo.bdaddr);

        ble_trace2("ADV start: %08x%04x\n",
                      (hello_sensor_hostinfo.bdaddr[5] << 24 ) + (hello_sensor_hostinfo.bdaddr[4] <<16) +
                      (hello_sensor_hostinfo.bdaddr[3] << 8 ) + hello_sensor_hostinfo.bdaddr[2],
                      (hello_sensor_hostinfo.bdaddr[1] << 8 ) + hello_sensor_hostinfo.bdaddr[0]);
    }
}

void hello_sensor_advertisement_stopped(void)
{
    ble_trace0("ADV stop!!!!\n");

    // If we are configured to stay connected, disconnection was caused by the
    // peer, start low advertisements, so that peer can connect when it wakes up.
    if (hello_sensor_stay_connected)
    {
        bleprofile_Discoverable(LOW_UNDIRECTED_DISCOVERABLE, hello_sensor_hostinfo.bdaddr);

        ble_trace2("ADV start: %08x%04x\n",
                      (hello_sensor_hostinfo.bdaddr[5] << 24 ) + (hello_sensor_hostinfo.bdaddr[4] <<16) +
                      (hello_sensor_hostinfo.bdaddr[3] << 8 ) + hello_sensor_hostinfo.bdaddr[2],
                      (hello_sensor_hostinfo.bdaddr[1] << 8 ) + hello_sensor_hostinfo.bdaddr[0]);
    }
}

void hello_sensor_timeout(UINT32 arg)
{
    ble_trace1("hello_sensor_timeout:%d\n", hello_sensor_timer_count);

    switch(arg)
    {
        case BLEPROFILE_GENERIC_APP_TIMER:
        {
            hello_sensor_timer_count++;
        }
        break;
    }
}

void hello_sensor_fine_timeout(UINT32 arg)
{
    hello_sensor_fine_timer_count++;

    //ble_trace1("hello_sensor_fine_timeout:%d\n", hello_sensor_fine_timer_count);

    // button control
    bleprofile_ReadButton();
}

//
// Process SMP bonding result.  If we successfully paired with the
// central device, save its BDADDR in the NVRAM and initialize
// associated data
//
void hello_sensor_smp_bond_result(LESMP_PARING_RESULT  result)
{
    ble_trace3("hello_sample, bond result %02x smpinfo addr type:%d emconninfo type:%d\n",
                result, lesmp_pinfo->lesmpkeys_bondedInfo.adrType, emconninfo_getPeerAddrType());


    // do some noise
    bleprofile_BUZBeep(bleprofile_p_cfg->buz_on_ms);

    if (result == LESMP_PAIRING_RESULT_BONDED)
    {
        // saving bd_addr in nvram
        UINT8 *bda;
        UINT8 writtenbyte;

        bda = (UINT8 *)emconninfo_getPeerPubAddr();

        memcpy(hello_sensor_hostinfo.bdaddr, bda, sizeof(BD_ADDR));
        hello_sensor_hostinfo.characteristic_client_configuration = 0;
        hello_sensor_hostinfo.number_of_blinks = 0;

        ble_trace2("Bond successful %08x%04x\n", (bda[5] << 24) + (bda[4] << 16) + (bda[3] << 8) + bda[2], (bda[1] << 8) + bda[0]);
        writtenbyte = bleprofile_WriteNVRAM(NVRAM_ID_HOST_LIST, sizeof(hello_sensor_hostinfo), (UINT8 *)&hello_sensor_hostinfo);
        ble_trace1("NVRAM write:%04x\n", writtenbyte);
    }
}

//
// Process notification from the stack that encryption has been set.  If connected
// client is registered for notification or indication, it is a good time to
// send it out
//
void hello_sensor_encryption_changed(HCI_EVT_HDR *evt)
{
    BLEPROFILE_DB_PDU db_pdu;

    UINT8 *bda = emconninfo_getPeerPubAddr();

    ble_trace2("hello_sample, encryption changed %08x%04x\n",
                (bda[5] << 24) + (bda[4] << 16) +
                (bda[3] << 8) + bda[2],
                (bda[1] << 8) + bda[0]);

    bleprofile_BUZBeep(bleprofile_p_cfg->buz_on_ms);

    // Connection has been encrypted meaning that we have correct/paired device
    // restore values in the database
    bleprofile_ReadNVRAM(NVRAM_ID_HOST_LIST, sizeof(hello_sensor_hostinfo), (UINT8 *)&hello_sensor_hostinfo);

    // Need to setup value of Client Configuration descriptor in our database because peer
    // might decide to read and stack sends answer without asking application.
    db_pdu.len = 2;
    db_pdu.pdu[0] = hello_sensor_hostinfo.characteristic_client_configuration & 0xff;
    db_pdu.pdu[1] = (hello_sensor_hostinfo.characteristic_client_configuration >> 8) & 0xff;

    bleprofile_WriteHandle(HANDLE_HELLO_SENSOR_CLIENT_CONFIGURATION_DESCRIPTOR, &db_pdu);

    // Setup value of our configuration in GATT database
    db_pdu.len = 1;
    db_pdu.pdu[0] = hello_sensor_hostinfo.number_of_blinks;

    bleprofile_WriteHandle(HANDLE_HELLO_SENSOR_CONFIGURATION, &db_pdu);

    ble_trace4("EncOn %08x%04x client_configuration:%04x blinks:%d\n",
                (hello_sensor_hostinfo.bdaddr[5] << 24) + (hello_sensor_hostinfo.bdaddr[4] << 16) +
                (hello_sensor_hostinfo.bdaddr[3] << 8) + hello_sensor_hostinfo.bdaddr[2],
                (hello_sensor_hostinfo.bdaddr[1] << 8) + hello_sensor_hostinfo.bdaddr[0],
                hello_sensor_hostinfo.characteristic_client_configuration,
                hello_sensor_hostinfo.number_of_blinks);

    // If there are outstanding messages that we could not send out because
    // connection was not up and/or encrypted, send them now.  If we are sending
    // indications, we can send only one and need to wait for ack.
    while ((hello_sensor_num_to_write != 0) && !hello_sensor_indication_sent)
    {
        hello_sensor_num_to_write--;
        hello_sensor_send_message();
    }

    // If configured to disconnect after delivering data, start idle timeout to do disconnection
    if (!hello_sensor_stay_connected && !hello_sensor_indication_sent)
    {
        bleprofile_StartConnIdleTimer(bleprofile_p_cfg->con_idle_timeout, bleprofile_appTimerCb);
        return;
    }

    // We are done with initial settings, and need to stay connected.  It is a good
    // time to slow down the pace of central polls to save power.  Following request asks
    // host to setup polling every 100-500 msec, with link supervision timeout 7 seconds.
    bleprofile_SendConnParamUpdateReq(80, 400, 0, 700);
}

//
// Check if client has registered for notification and indication and send message if appropriate
//
void hello_sensor_send_message(void)
{
    BLEPROFILE_DB_PDU db_pdu;

    // If client has not registered for indication or notification, do not need to do anything
    if (hello_sensor_hostinfo.characteristic_client_configuration == 0)
        return;

    // Read value of the characteristic to send from the GATT DB.
    bleprofile_ReadHandle(HANDLE_HELLO_SENSOR_VALUE_NOTIFY, &db_pdu);
    ble_tracen((char *)db_pdu.pdu, db_pdu.len);
    ble_trace0("\n");

    if (hello_sensor_hostinfo.characteristic_client_configuration & CCC_NOTIFICATION)
    {
        bleprofile_sendNotification(HANDLE_HELLO_SENSOR_VALUE_NOTIFY, (UINT8 *)db_pdu.pdu, db_pdu.len);
    }
    else
    {
        if (!hello_sensor_indication_sent)
        {
            hello_sensor_indication_sent = TRUE;
            bleprofile_sendIndication(HANDLE_HELLO_SENSOR_VALUE_NOTIFY, (UINT8 *)db_pdu.pdu, db_pdu.len, hello_sensor_indication_cfm);
        }
    }
}

//
// Process write request or command from peer device
//
int hello_sensor_write_handler(LEGATTDB_ENTRY_HDR *p)
{
    UINT8  writtenbyte;
    UINT16 handle   = legattdb_getHandle(p);
    int    len      = legattdb_getAttrValueLen(p);
    UINT8  *attrPtr = legattdb_getAttrValue(p);

    // do some noise
    bleprofile_BUZBeep(bleprofile_p_cfg->buz_on_ms);

    ble_trace1("hello_sensor_write_handler: handle %04x\n", handle);

    // By writing into Characteristic Client Configuration descriptor
    // peer can enable or disable notification or indication
    if ((len == 2) && (handle == HANDLE_HELLO_SENSOR_CLIENT_CONFIGURATION_DESCRIPTOR))
    {
        hello_sensor_hostinfo.characteristic_client_configuration = attrPtr[0] + (attrPtr[1] << 8);
        ble_trace1("hello_sensor_write_handler: client_configuration %04x\n", hello_sensor_hostinfo.characteristic_client_configuration);
    }
    // User can change number of blinks to send when button is pushed
    else if ((len == 1) && (handle == HANDLE_HELLO_SENSOR_CONFIGURATION))
    {
        hello_sensor_hostinfo.number_of_blinks = attrPtr[0];
    if (hello_sensor_hostinfo.number_of_blinks != 0)
    {
        bleprofile_LEDBlink(500, 500, hello_sensor_hostinfo.number_of_blinks);
    }
    }
    else
    {
        ble_trace2("hello_sensor_write_handler: bad write len:%d handle:0x%x\n", len, handle);
        return 0x80;
    }
    // Save update to NVRAM.  Client does not need to set it on every connection.
    writtenbyte = bleprofile_WriteNVRAM(NVRAM_ID_HOST_LIST, sizeof(hello_sensor_hostinfo), (UINT8 *)&hello_sensor_hostinfo);
    ble_trace1("hello_sensor_write_handler: NVRAM write:%04x\n", writtenbyte);

    return 0;
}

// Three Interrupt inputs (Buttons) can be handled here.
// If the following value == 1, Button is pressed. Different than initial value.
// If the following value == 0, Button is depressed. Same as initial value.
// Button1 : value&0x01
// Button2 : (value&0x02)>>1
// Button3 : (value&0x04)>>2
void hello_sensor_interrupt_handler(UINT8 value)
{
    BLEPROFILE_DB_PDU db_pdu;
    static uint32_t previous_button_pressed_timer = 0;
    static uint32_t current_button_pressed_timer = 0;

    current_button_pressed_timer = hello_sensor_fine_timer_count;

    ble_trace3("\n(INT)But1:%d But2:%d But3:%d\n", value&0x01, (value& 0x02) >> 1, (value & 0x04) >> 2);

    ble_trace2("\n(INT)current %d, previous %d\n", current_button_pressed_timer, previous_button_pressed_timer);

    if ((current_button_pressed_timer - previous_button_pressed_timer) > 1)
    {
        ble_trace0("Button pressed.\n");
        previous_button_pressed_timer = current_button_pressed_timer;

        // Blink as configured
        bleprofile_LEDBlink(500, 500, hello_sensor_hostinfo.number_of_blinks);

        // keep number of the button pushes in the last byte of the Hello %d message.  That will
        // guarantee that if client reads it, it will have correct data.
        bleprofile_ReadHandle(HANDLE_HELLO_SENSOR_VALUE_NOTIFY, &db_pdu);
        ble_tracen((char *)db_pdu.pdu, db_pdu.len);
        ble_trace0("\n");
        db_pdu.pdu[6]++;
        if (db_pdu.pdu[6] > '9')
            db_pdu.pdu[6] = '0';
        bleprofile_WriteHandle(HANDLE_HELLO_SENSOR_VALUE_NOTIFY, &db_pdu);

        // remember how many messages we need to send
        hello_sensor_num_to_write++;

        // If connection is down, we need to start advertisements, so that client can connect
        if (hello_sensor_connection_handle == 0)
        {
            bleprofile_Discoverable(HIGH_UNDIRECTED_DISCOVERABLE, hello_sensor_remote_addr);

            ble_trace2("ADV start high: %08x%04x\n",
                        (hello_sensor_hostinfo.bdaddr[5] << 24) + (hello_sensor_hostinfo.bdaddr[4] << 16) +
                        (hello_sensor_hostinfo.bdaddr[3] << 8) + hello_sensor_hostinfo.bdaddr[2],
                        (hello_sensor_hostinfo.bdaddr[1] << 8) + hello_sensor_hostinfo.bdaddr[0]);
            return;
        }
        // Connection is up. Send message if client is registered to receive indication
        //  or notification.  After we sent an indication we need to wait for the ack before
        // we can send anything else
        while ((hello_sensor_num_to_write != 0) && !hello_sensor_indication_sent)
        {
            hello_sensor_num_to_write--;
            hello_sensor_send_message();
        }

        // if we sent all messages, start connection idle timer to disconnect
        if (!hello_sensor_stay_connected && !hello_sensor_indication_sent)
        {
            bleprofile_StartConnIdleTimer(bleprofile_p_cfg->con_idle_timeout, bleprofile_appTimerCb);
        }
    }
}

// process indication confirmation.  if client wanted us to use indication instead of notifications
// we have to wait for confirmation after every message sent.  For example if user pushed button
// twice very fast we will send first message, wait for confirmation, send second message, wait
// for confirmation and if configured start idle timer only after that.
void hello_sensor_indication_cfm(void)
{
    if (!hello_sensor_indication_sent)
    {
        ble_trace0("Hello: Wrong Confirmation!!!\n");
        return;
    }
    hello_sensor_indication_sent = 0;

    // We might need to send more indications
    if (hello_sensor_num_to_write)
    {
        hello_sensor_num_to_write--;
        hello_sensor_send_message();
    }
    // if we sent all messages, start connection idle timer to disconnect
    if (!hello_sensor_stay_connected && !hello_sensor_indication_sent)
    {
        bleprofile_StartConnIdleTimer(bleprofile_p_cfg->con_idle_timeout, bleprofile_appTimerCb);
    }
}
