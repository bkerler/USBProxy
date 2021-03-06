/*
 * Copyright 2013 Dominic Spill
 * Copyright 2013 Adam Stasiak
 *
 * This file is part of USBProxy.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 *
 * Endpoint.cpp
 *
 * Created on: Nov 6, 2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "DeviceProxy.h"
#include "HexString.h"

#include "Endpoint.h"
#include "Interface.h"

Endpoint::Endpoint(Interface* _interface,const __u8* p) {
	interface=_interface;
	memcpy(&descriptor,p,7);
}

Endpoint::Endpoint(Interface* _interface,const usb_endpoint_descriptor* _descriptor) {
	interface=_interface;
	descriptor=*_descriptor;
}

Endpoint::Endpoint(Interface* _interface,__u8 bEndpointAddress,__u8 bmAttributes,__u16 wMaxPacketSize,__u8 bInterval) {
	interface=_interface;
	descriptor.bLength=7;
	descriptor.bDescriptorType=USB_DT_ENDPOINT;
	descriptor.bEndpointAddress=bEndpointAddress;
	descriptor.bmAttributes=bmAttributes;
	descriptor.wMaxPacketSize=wMaxPacketSize;
	descriptor.bInterval=bInterval;
}

Endpoint::~Endpoint() {
}

const usb_endpoint_descriptor* Endpoint::get_descriptor() {
	return &descriptor;
}

size_t Endpoint::get_full_descriptor_length() {
	return descriptor.bLength;
}

void Endpoint::get_full_descriptor(__u8** p) {
	memcpy(*p,&descriptor,descriptor.bLength);
	*p=*p+descriptor.bLength;
}

void Endpoint::print(__u8 tabs) {
	char* hex=hex_string(&descriptor,descriptor.bLength);
	printf("%.*sEP(%02x): %s\n",tabs,TABPADDING,descriptor.bEndpointAddress,hex);
	free(hex);
}

const definition_error Endpoint::is_defined(__u8 configId,__u8 interfaceNum,__u8 interfaceAlternate) {
	if (descriptor.bLength!=7) {return definition_error(DE_ERR_INVALID_DESCRIPTOR,0x01, DE_OBJ_ENDPOINT,configId,interfaceNum,interfaceAlternate,descriptor.bEndpointAddress);}
	if (descriptor.bDescriptorType!=USB_DT_ENDPOINT) {return definition_error(DE_ERR_INVALID_DESCRIPTOR,0x02, DE_OBJ_ENDPOINT,configId,interfaceNum,interfaceAlternate,descriptor.bEndpointAddress);}
	if (descriptor.bEndpointAddress&0x70) {return definition_error(DE_ERR_INVALID_DESCRIPTOR,0x03, DE_OBJ_ENDPOINT,configId,interfaceNum,interfaceAlternate,descriptor.bEndpointAddress);}
	if ( (descriptor.bmAttributes&0xC0) ||
		(((descriptor.bmAttributes&0x03)!=1) && (descriptor.bmAttributes&0xFC))
			) {return definition_error(DE_ERR_INVALID_DESCRIPTOR,0x04, DE_OBJ_ENDPOINT,configId,interfaceNum,interfaceAlternate,descriptor.bEndpointAddress);}
	//__u8 wMaxPacketSize
	if (
			(((descriptor.bmAttributes&0x03)==1) && descriptor.bInterval!=1) ||
			(((descriptor.bmAttributes&0x03)==3) && descriptor.bInterval==1)
		) {return definition_error(DE_ERR_INVALID_DESCRIPTOR,0x05, DE_OBJ_ENDPOINT,configId,interfaceNum,interfaceAlternate,descriptor.bEndpointAddress);}
	return definition_error();
}

Interface* Endpoint::get_interface() {return interface;}
