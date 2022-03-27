/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb.h"

#define USB_VID                        0xeb98
#define USB_PID                        0x53f7

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
static const tusb_desc_device_t desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Use Interface Association Descriptor (IAD) for CDC
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
extern "C" const uint8_t*
tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum
{
	ITF_NUM_DFU0 = 0,
	ITF_NUM_TOTAL
};

// Number of Alternate Interface (each for 1 flash partition)
#define DFU_ALT_COUNT   1

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_DFU_DESC_LEN(DFU_ALT_COUNT))

#define FUNC_ATTRS (DFU_ATTR_CAN_UPLOAD | DFU_ATTR_CAN_DOWNLOAD | DFU_ATTR_MANIFESTATION_TOLERANT)

const uint8_t desc_fs_configuration[] =
{
	// Config number, interface count, string index, total length, attribute, power in mA
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

	// Interface number, string index, attributes, detach timeout, transfer size */
	TUD_DFU_DESCRIPTOR(ITF_NUM_DFU0, DFU_ALT_COUNT, 4, FUNC_ATTRS, 1000, CFG_TUD_DFU_XFER_BUFSIZE),
};


// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
extern "C" modm_weak const uint8_t*
tud_descriptor_configuration_cb(uint8_t index)
{
	(void)index;  // for multiple configurations
	return desc_fs_configuration;
}

//
//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
const char*
string_desc_arr[] =
{
    NULL,               // 0: Language
    "UW MISL",          // 1: Manufacturer
    "PurpleDrop Bootloader",       // 2: Product
    NULL,               // 3: Serial, filled in programmatically in tud_descriptor_string_cb
    "Flash",            // 4: DFU Interface
};

static uint16_t _desc_str[33];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long
// enough for transfer to complete
extern "C" const uint16_t*
tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	(void)langid;
	uint8_t chr_count = 1;

	if (index == 0)
	{	// supported language is English (0x0409)
		_desc_str[1] = 0x09; _desc_str[2] = 0x04;
	}
	else if (index == 3)
	{
		extern uint8_t tusb_get_device_serial(uint16_t* serial_str);
		chr_count = tusb_get_device_serial(_desc_str + 1);
	}
	else
	{
		if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
			return NULL;

		const char* str = string_desc_arr[index];

		// Cap at max char
		chr_count = strlen(str);
		if (chr_count > 31) chr_count = 31;

		// Convert ASCII string into UTF-16
		for (uint8_t i = 0; i < chr_count; i++)
			_desc_str[1 + i] = str[i];
	}

	// first byte is length (including header), second byte is string type
	_desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

	return _desc_str;
}