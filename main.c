// Copyright 2020 Mikhail ysph Subbotin

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <libusb-1.0/libusb.h>

#include "mouselist.h"
#include "miscellaneous.h"

#define LIBUSB_OPTION_LOG_LEVEL	0
#define LIBUSB_LOG_LEVEL_ERROR	1

static libusb_device_handle *devh = NULL;
libusb_context *global_context;

static int source, type, R, G, B;
static const int bmRequestType = 0x21; // type and recipient: 0b00100001
static const int bRequest = 0x09; // type of request: set_report
static const int wValue = 0x0211; // report type and id: output, 0x11
int wIndex, returnCode, found = 0;

Item* available_head; // the list contains available devices

//temporary
int temp_id;

void CloseDeviceAndExit(void) {
	if (devh)
		libusb_close(devh);
	libusb_exit(NULL);
}

void DetachKernel(void) {
	if (libusb_kernel_driver_active(devh, wIndex)) {
		libusb_detach_kernel_driver(devh, wIndex);
	}

	returnCode = libusb_claim_interface(devh, wIndex);

	if (returnCode < 0) {
		fprintf(stderr, "Error: Cannot claim interface: %s\n",
		libusb_error_name(returnCode));

		CloseDeviceAndExit();
		return;
	}
}

void AttachKernel(void) {
	libusb_release_interface(devh, wIndex);

	if (!libusb_kernel_driver_active(devh, wIndex)) {
		libusb_attach_kernel_driver(devh, wIndex);
	}
}

int openDevice(void) {
	const int available = getSize(available_head);
	int choice;
	char input_string[20];

	printf("\nChoose what device you would like to operate on. Available devices:\n");
	printAllItems(available_head);
	printf("Enter [0] to exit.\n");

	LOOP:
		fgets(input_string, 20, stdin);
		choice = strtol(input_string, NULL, 0);
		if ((choice < 0) || (choice > available)) {
			printf("Choose correct number or exit!\n");
			fflush(stdin);
			goto LOOP;
		} else if (choice == 0) {
			printf("Exiting...\n");
			fflush(stdin);
			return 2;
		}

		printf("\nEnter source (0 for primary, 1 for logo): \n");
		printf("For primary: Color is set by user input (RGB values).\n");
		printf("For logo: Color is changed dynamically, no input needed.\n");
		while (1) {
			fgets(input_string, sizeof(input_string), stdin);
			source = strtol(input_string, NULL, 10);
			if (source == 0 || source == 1) {
				break;
			} else {
				printf("Invalid input. Please enter 0 for primary or 1 for logo.\n");
			}
		}

		if (source == 0) {
			// For primary, ask for RGB values
			printf("\nEnter RGB values (decimal or hex format).\n");
			printf("For decimal: Enter 'R,G,B' (e.g., 255,87,51)\n");
			printf("For hex: Enter '#RRGGBB' (e.g., #FF5733)\n");

			char input[20];
			while (1) {
				fgets(input, sizeof(input), stdin);
				input[strcspn(input, "\n")] = '\0';  // Remove newline character

				// Check if input starts with '#' for hex format
				if (input[0] == '#') {
					if (strlen(input) == 7) {
						// Parse hex code
						if (sscanf(input, "#%2x%2x%2x", &R, &G, &B) == 3) {
							printf("Parsed Hex: R = %d, G = %d, B = %d\n", R, G, B);
							break;
						} else {
							printf("Invalid hex code format. Please use #RRGGBB format.\n");
						}
					} else {
						printf("Invalid hex code. Ensure it starts with '#' and has exactly 6 hex characters.\n");
					}
				} else {
					// Otherwise, assume decimal format
					// Parse the RGB values separated by commas
					if (sscanf(input, "%d,%d,%d", &R, &G, &B) == 3) {
						// Check if values are in range 0-255
						if (R >= 0 && R <= 255 && G >= 0 && G <= 255 && B >= 0 && B <= 255) {
							printf("Parsed Decimal: R = %d, G = %d, B = %d\n", R, G, B);
							break;
						} else {
							printf("Invalid RGB values. Ensure each value is between 0 and 255.\n");
						}
					} else {
						printf("Invalid format. Please enter three RGB values separated by commas (e.g., 255,87,51).\n");
					}
				}

				// Clear the input buffer in case of invalid input
				while (getchar() != '\n');
			}
		} else if (source == 1) {
			// For logo, change color dynamically (no input)
			R = 0;
			G = 0;
			B = 0;
		}

	const int needed_id = getNthId(available_head, choice);
	const char* temp_name = getName(available_head, needed_id);

	//open device
	devh = libusb_open_device_with_vid_pid(NULL, ID_VENDOR, needed_id);
	if (!devh) {
		fprintf(stderr, "Error: Cannot open %s\n", temp_name);
		return -1;
	}
	printf("\nDevice %s is operating...\n", temp_name);

	//process
	srand((unsigned)time(NULL));
	wIndex = getInterface(available_head, needed_id);
	int devByte[4];

	// we dont choose what we change yet
	// devByte[0] is changed to 0x10 when we change dpi or response rate
	// devByte[3] is changing as well
	devByte[0] = 0x11;
	devByte[2] = getByte3(available_head, needed_id);
	devByte[3] = 0x3b;
	// exclusive option for logitech pro wireless
	switch (wIndex) {
		case 1:
			devByte[1] = 0xff;
			break;
		case 2:
			devByte[1] = 0x01;
			break;
		default:
			printf("Error: Wrong interface!\n");
			return -1;
	}

	if (needed_id == 0xc088) {
		wIndex = 2;
		devByte[1] = 0xff;
	}

	type = 0x01; // static

	unsigned char data[20] = {devByte[0], devByte[1], devByte[2], devByte[3], source, type, R, G, B, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	/*  detach kernel
		&
		claim the interface on a given device handle */
	DetachKernel();
	returnCode = libusb_control_transfer(devh, bmRequestType, bRequest, wValue,
										 wIndex, data, sizeof(data), 2000);
	if (returnCode < 0) {
		fprintf(stderr, "Error: Cannot transfer control data: %s\n", libusb_error_name(returnCode));
	}

	/*  release the interface previously claimed
		&
		attach kernel */
	AttachKernel();
	
	if (devh)
		libusb_close(devh);

	return EXIT_SUCCESS;
}

int getDevice(Item* head) {
	libusb_device **list;
	struct libusb_device_descriptor desc;

	int i;
	ssize_t count = libusb_get_device_list(global_context, &list);

	for (i = 0; i < count; ++i) {
		libusb_device *device = list[i];

		if (!libusb_get_device_descriptor(device, &desc)) {
			if (desc.idProduct == ID_PRODUCT_UNIDENTIFIED) {
				printf("Found wireless logitech device, but it's UNIDENTIFIED.\n");
				printf("Consider upgrading the kernel to at least version of 5.2.\nOr use wired option of your mouse.\n\n");
				continue;
			}

			if (ID_VENDOR == desc.idVendor && searchItem(head, desc.idProduct)) {
				const char* temp_name = getName(head, desc.idProduct);
				const int temp_interface = getInterface(head, desc.idProduct);
				const int temp_byte3 = getByte3(head, desc.idProduct);

				pushItem(&available_head, desc.idProduct, temp_name, temp_interface, temp_byte3);
				printf("\nDevice id=0x%x, name=%s, interface=%x - has been found!\n", desc.idProduct, temp_name, temp_interface);
				found++;
			}
		}
	}
	if (!found) return found;
	libusb_free_device_list(list, 1);

	return 1;
}

/**
 * @brief Check if the device is in the list of unsupported devices
 * @param head - the list of unsupported devices, which is **deleted** after the check
 * @return EXIT_SUCCESS if the device is supported, EXIT_FAILURE otherwise
*/
int unsupportedDevice(Item* head) {
	libusb_device **list;
	struct libusb_device_descriptor desc;

	int i;
	ssize_t count = libusb_get_device_list(global_context, &list);

	for (i = 0; i < count; ++i) {
		libusb_device *device = list[i];

		if (!libusb_get_device_descriptor(device, &desc)) {
			if (ID_VENDOR == desc.idVendor && searchItem(head, desc.idProduct)) {
				const char* temp_name = getName(head, desc.idProduct);
				printf("\nDevice id=0x%x, name=%s - is not supported yet!\n", desc.idProduct, temp_name);
				libusb_free_device_list(list, 1);
				deleteLinkedList(&head);
				return EXIT_FAILURE;
			}
		}
	}
	libusb_free_device_list(list, 1);
	deleteLinkedList(&head);
	return EXIT_SUCCESS;
}

int main(void) {
	// init
	returnCode = libusb_init(NULL);

	#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000106) // >=1.0.22
		libusb_set_option(global_context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_ERROR);
	#else
		libusb_set_debug(global_context, LIBUSB_LOG_LEVEL_ERROR);
	#endif

	if (returnCode < 0) {
		fprintf(stderr, "Error: Cannot initialize libusb. %s\n", libusb_error_name(returnCode));

		return returnCode;
	}

	// add known devices
	Item* head = (Item*)malloc(size_of_Item);
	head->next = NULL;
	Item* unsuported = (Item*)malloc(size_of_Item);
	unsuported->next = NULL;
	pushItem(&head, 0xc092, "G102-G203 LIGHTSYNC", WIRED_OR_CABLE, 0x0e);
	pushItem(&head, 0xc084, "G203 Prodigy", WIRED_OR_CABLE, 0x0e);
	pushItem(&head, 0xc083, "G403 Prodigy", WIRED_OR_CABLE, 0x0e);
	pushItem(&unsuported, 0xc07f, "G302 Daedalus Prime", WIRED_OR_CABLE,-1);
	pushItem(&unsuported, 0xc080, "G303 Daedalus Apex", WIRED_OR_CABLE,-1);
	pushItem(&unsuported, 0x4074, "G305 Lightspeed Wireless", WIRELESS_RECEIVER,-1);
	pushItem(&unsuported, 0xc07e, "G402 Hyperion Fury", WIRED_OR_CABLE,-1);
	pushItem(&unsuported, 0xc08f, "G403 Hero", WIRED_OR_CABLE, -1);
	pushItem(&head, 0xc082, "G403 Wireless", WIRED_OR_CABLE, 0x18);
	pushItem(&head, 0x405d, "G403 Wireless", WIRELESS_RECEIVER, 0x18);
	pushItem(&unsuported, 0xc07d, "G502 Proteus Core", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0xc08b, "G502 Hero", WIRED_OR_CABLE,-1);
	pushItem(&head, 0xc332, "G502 Proteus Spectrum", WIRED_OR_CABLE, 0x02);
	pushItem(&unsuported, 0xc08d, "G502 Lightspeed Wireless", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0x407f, "G502 Lightspeed Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&unsuported, 0xc08e, "MX518", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0xc24a, "G600 MMO", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0xc537, "G602 Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&unsuported, 0x406c, "G603 Lightspeed Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&unsuported, 0xb024, "G604 Lightspeed Wireless", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0x4085, "G604 Lightspeed Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&head, 0xc087, "G703 Lightspeed Wireless", WIRED_OR_CABLE, 0x18);
	pushItem(&head, 0x4070, "G703 Lightspeed Wireless", WIRELESS_RECEIVER, 0x18);
	pushItem(&unsuported, 0xc090, "G703 Lightspeed Hero Wireless", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0x4086, "G703 Lightspeed Hero Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&unsuported, 0xc081, "G900 Chaos Spectrum Wireless", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0x4053, "G900 Chaos Spectrum Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&unsuported, 0xc086, "G903 Lightspeed Wireless", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0x4067, "G903 Lightspeed Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&unsuported, 0xc091, "G903 Lightspeed Hero Wireless", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0x4087, "G903 Lightspeed Hero Wireless", WIRELESS_RECEIVER, -1);
	pushItem(&unsuported, 0xc085, "PRO", WIRED_OR_CABLE, -1);
	pushItem(&unsuported, 0xc08c, "PRO HERO", WIRED_OR_CABLE, -1);
	pushItem(&head, 0xc088, "PRO Wireless", WIRED_OR_CABLE, 0x07);
	pushItem(&head, 0x4079, "PRO Wireless", WIRELESS_RECEIVER, 0x07);


	// list for available devices
	available_head = (Item*)malloc(size_of_Item);
	available_head->next = NULL;

	// check unsupported devices
	returnCode = unsupportedDevice(unsuported);
	if (returnCode == EXIT_FAILURE){
		deleteLinkedList(&head);
		deleteLinkedList(&available_head);
		CloseDeviceAndExit();
		return EXIT_FAILURE;
	}
	

	// find device
	returnCode = getDevice(head);
	if (!returnCode) {
		fprintf(stderr, "Error: Cannot find any logitech mouse. %s\n", libusb_error_name(returnCode));
		CloseDeviceAndExit();

		return returnCode;
	}

	returnCode = openDevice();
	if (returnCode == 2) {
		deleteLinkedList(&head);
		deleteLinkedList(&available_head);
		CloseDeviceAndExit();
		return EXIT_SUCCESS;
	}
	if (returnCode < 0) {
		fprintf(stderr, "Error: Cannot operate logitech mouse. %s\n", libusb_error_name(returnCode));
		CloseDeviceAndExit();
		return EXIT_FAILURE;
	}

	if (returnCode >= 0) {
		switch (source) {
			case 0:
				printf("Now, the color of your primary is #%02x%02x%02x\n",R,G,B);
				break;
			case 1:
				printf("Now, the color is set to logo\n");
				break;
			default:
				printf("undefined!\n");
				deleteLinkedList(&head);
				deleteLinkedList(&available_head);
				exit(EXIT_FAILURE);
		}
	}

	deleteLinkedList(&head);
	deleteLinkedList(&available_head);

	libusb_exit(NULL);

	return EXIT_SUCCESS;
}
