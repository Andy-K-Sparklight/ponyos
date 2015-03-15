/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 *
 * Mouse driver
 *
 */
#include <system.h>
#include <logging.h>
#include <pipe.h>
#include <module.h>
#include <mouse.h>

static uint8_t mouse_cycle = 0;
static int8_t  mouse_byte[3];

#define PACKETS_IN_PIPE 1024
#define DISCARD_POINT 32

#define MOUSE_IRQ 12

#define MOUSE_PORT   0x60
#define MOUSE_STATUS 0x64
#define MOUSE_ABIT   0x02
#define MOUSE_BBIT   0x01
#define MOUSE_WRITE  0xD4
#define MOUSE_F_BIT  0x20
#define MOUSE_V_BIT  0x08

static fs_node_t * mouse_pipe;

static void mouse_wait(uint8_t a_type) {
	uint32_t timeout = 100000;
	if (!a_type) {
		while (--timeout) {
			if ((inportb(MOUSE_STATUS) & MOUSE_BBIT) == 1) {
				return;
			}
		}
		debug_print(INFO, "mouse timeout");
		return;
	} else {
		while (--timeout) {
			if (!((inportb(MOUSE_STATUS) & MOUSE_ABIT))) {
				return;
			}
		}
		debug_print(INFO, "mouse timeout");
		return;
	}
}

static void mouse_write(uint8_t write) {
	mouse_wait(1);
	outportb(MOUSE_STATUS, MOUSE_WRITE);
	mouse_wait(1);
	outportb(MOUSE_PORT, write);
}

static uint8_t mouse_read(void) {
	mouse_wait(0);
	char t = inportb(MOUSE_PORT);
	return t;
}

static void mouse_handler(struct regs *r) {
	uint8_t status = inportb(MOUSE_STATUS);
	while (status & MOUSE_BBIT) {
		int8_t mouse_in = inportb(MOUSE_PORT);
		if (status & MOUSE_F_BIT) {
			switch (mouse_cycle) {
				case 0:
					mouse_byte[0] = mouse_in;
					if (!(mouse_in & MOUSE_V_BIT)) return;
					++mouse_cycle;
					break;
				case 1:
					mouse_byte[1] = mouse_in;
					++mouse_cycle;
					break;
				case 2:
					mouse_byte[2] = mouse_in;
					/* We now have a full mouse packet ready to use */
					if (mouse_byte[0] & 0x80 || mouse_byte[0] & 0x40) {
						/* x/y overflow? bad packet! */
						break;
					}
					mouse_device_packet_t packet;
					packet.magic = MOUSE_MAGIC;
					packet.x_difference = mouse_byte[1];
					packet.y_difference = mouse_byte[2];
					packet.buttons = 0;
					if (mouse_byte[0] & 0x01) {
						packet.buttons |= LEFT_CLICK;
					}
					if (mouse_byte[0] & 0x02) {
						packet.buttons |= RIGHT_CLICK;
					}
					if (mouse_byte[0] & 0x04) {
						packet.buttons |= MIDDLE_CLICK;
					}
					mouse_cycle = 0;

					mouse_device_packet_t bitbucket;
					while (pipe_size(mouse_pipe) > (int)(DISCARD_POINT * sizeof(packet))) {
						read_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&bitbucket);
					}
					write_fs(mouse_pipe, 0, sizeof(packet), (uint8_t *)&packet);
					break;
			}
		}
		status = inportb(MOUSE_STATUS);
	}
	irq_ack(MOUSE_IRQ);
}

static int ioctl_mouse(fs_node_t * node, int request, void * argp) {
	if (request == 1) {
		mouse_cycle = 0;
		return 0;
	}
	return -1;
}

static int mouse_install(void) {
	debug_print(NOTICE, "Initializing PS/2 mouse interface");
	uint8_t status;
	IRQ_OFF;
	mouse_pipe = make_pipe(sizeof(mouse_device_packet_t) * PACKETS_IN_PIPE);
	mouse_wait(1);
	outportb(MOUSE_STATUS, 0xA8);
	mouse_wait(1);
	outportb(MOUSE_STATUS, 0x20);
	mouse_wait(0);
	status = inportb(0x60) | 2;
	mouse_wait(1);
	outportb(MOUSE_STATUS, 0x60);
	mouse_wait(1);
	outportb(MOUSE_PORT, status);
	mouse_write(0xF6);
	mouse_read();
	mouse_write(0xF4);
	mouse_read();
	IRQ_RES;
	irq_install_handler(MOUSE_IRQ, mouse_handler);

	uint8_t tmp = inportb(0x61);
	outportb(0x61, tmp | 0x80);
	outportb(0x61, tmp & 0x7F);
	inportb(MOUSE_PORT);

	mouse_pipe->flags = FS_CHARDEVICE;
	mouse_pipe->ioctl = ioctl_mouse;

	vfs_mount("/dev/mouse", mouse_pipe);
	return 0;
}

static int mouse_uninstall(void) {
	/* TODO */
	return 0;
}

MODULE_DEF(ps2mouse, mouse_install, mouse_uninstall);
