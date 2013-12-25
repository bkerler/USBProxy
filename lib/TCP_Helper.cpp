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
 * TCP_Helper.cpp
 *
 * Created on: Dec 19, 2013
 */

#include "TCP_Helper.h"
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <fcntl.h>

#define BASE_PORT 10400
#define TCP_BUFFER_SIZE 1456

int TCP_Helper::debugLevel=0;

TCP_Helper::TCP_Helper(const char* address) {
	p_server = address==NULL;
	p_address = strdup(address);
	p_is_connected = false;
	int i;
	for (i=0;i<32;i++) {
		ep_listener[i]=0;
		ep_socket[i]=0;
		ep_buf[i]=NULL;
	}
}

TCP_Helper::~TCP_Helper() {
	disconnect();
	free(p_address);
}

int TCP_Helper::connect(int timeout) {
	//sized to handle ETHERNET less IP(20 byte)/TCP(max 24 byte) headers
	
	int rc;
	if(p_server) {
		server_listen(0);
		rc=server_connect(0,timeout);
	} else
		rc=client_connect(0,timeout);
	
	p_is_connected=rc==0;
	if (p_is_connected) ep_buf[0] = (__u8*)malloc(TCP_BUFFER_SIZE);
	return rc;
}

int TCP_Helper::client_connect(int port,int timeout) {
	int sck;
	if (ep_connect[port] && ep_socket[port]) return 0;
	if (!ep_socket[port]) { //create socket
		struct sockaddr_in serv_addr;

		memset(ep_buf[port], 0, TCP_BUFFER_SIZE);
		if((sck = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0))< 0)
		{
			printf("\n Error : Could not create socket \n");
			return -1;
		}

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(BASE_PORT + port);
		serv_addr.sin_addr.s_addr = inet_addr(p_address);

		int rc=::connect(sck, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	
		if (rc==0) { //immediate connect
			ep_socket[port]=sck;
			ep_connect[port]=true;
			return 0;
		}
		if(rc<0 && errno==EINPROGRESS) { //async connect
			ep_socket[port]=sck;
			ep_connect[port]=false;
		}
		if (rc<0 && errno!=EINPROGRESS) {
			fprintf(stderr, "Error : Connect Failed \n");
			return -1;
		}
	}
	if (ep_socket[port]) { //wait for an ascyn connect to complete
		struct pollfd poll_connect;
		poll_connect.fd=sck;
		poll_connect.events=POLLOUT;
		if (poll(&poll_connect,1,timeout) && (poll_connect.revents&POLLOUT)) {
			ep_connect[port]=true;
			return 0;
		} else {
			return ETIMEDOUT;
		}
	}
	return -1; //should never really be reached
}

void TCP_Helper::server_listen(int port) {
	int sck;
	if (ep_listener[port]) return;

	struct sockaddr_in serv_addr = {};

	sck = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, 0);
	fprintf(stderr, "socket retrieve success\n");
	
	memset(ep_buf[port], '0', sizeof(TCP_BUFFER_SIZE));

	serv_addr.sin_family = AF_INET;    
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
	serv_addr.sin_port = htons(BASE_PORT + port);    
	
	bind(sck, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	
	if(listen(sck, 10) == -1){
		fprintf(stderr, "Failed to listen\n");
		return;
	}
	ep_connect[port]=false;
	ep_listener[port]=sck;
}

int TCP_Helper::server_connect(int port,int timeout) {
	struct pollfd poll_accept;
	poll_accept.fd=ep_listener[port];
	poll_accept.events=POLLIN;
	if (poll(&poll_accept,1,timeout) && poll_accept.revents==POLLIN) {
		struct sockaddr client_addr;
		unsigned int len=sizeof(client_addr);
		int rc=accept(ep_listener[port],&client_addr,&len);
		if (rc<0) {
			fprintf(stderr,"Accept failed.\n");
			return -1;
		} else {
			fcntl(rc,F_SETFD,FD_CLOEXEC);
			ep_socket[port]=rc;
			ep_connect[port]=true;
		}
	} else {
		return ETIMEDOUT;
	}
	return -1;
}

void TCP_Helper::disconnect() {
	int i;
	for(i=0;i<32;i++) {
		if (ep_listener[i]) {close(ep_listener[i]);ep_listener[i]=0;}
		if (ep_socket[i]) {close(ep_socket[i]);ep_socket[i]=0;}
		if (ep_buf[i]) {free(ep_buf[i]);ep_buf[i]=NULL;}
	}
}


int TCP_Helper::client_open_endpoints(__u8* eps,__u8 num_eps,int timeout) {
	int i;
	struct pollfd* poll_connections=NULL;
	__u8* poll_idxs=NULL;
	int poll_count=0;
	int poll_idx=0;

	for (i=0;i<num_eps;i++) {
		int idx=(eps[i]&0x80)?(eps[i]&0x1f)|0x20:eps[i];
		if (!ep_connect[idx]) {
			if (!ep_socket[i]) {client_connect(idx,0);}
			if (!ep_connect[idx]) poll_count++;
		}
	}
	poll_connections=(struct pollfd*)calloc(poll_count,sizeof(struct pollfd));
	poll_idxs=(__u8*)calloc(poll_count,sizeof(__u8));
	for (i=0;i<num_eps;i++) {
		int idx=(eps[i]&0x80)?(eps[i]&0x1f)|0x20:eps[i];
		if (!ep_connect[idx]) {
			poll_connections[poll_idx].fd=ep_socket[i];
			poll_connections[poll_idx].events=POLLOUT;
			poll_idxs[poll_idx]=idx;
			poll_idx++;
		}
	}

	int ep_count=poll_count;
	int rc=-1;
	if (poll(poll_connections,poll_count,timeout)) {
		for (poll_idx=0;i<poll_count;poll_idx++) {
			if (poll_connections[poll_idx].revents==POLLOUT) {
				int idx=poll_idxs[poll_idx];
				if (client_connect(idx,0)) {
					ep_buf[idx] = (__u8*)malloc(TCP_BUFFER_SIZE);
					ep_count--;
				}
			}
		}
		rc=ep_count;
	} else {
		rc=ETIMEDOUT;
	}
	free(poll_connections);
	poll_connections=NULL;
	free(poll_idxs);
	poll_idxs=NULL;
	return rc;
}

int TCP_Helper::server_open_endpoints(__u8* eps,__u8 num_eps,int timeout) {
	int i;
	struct pollfd* poll_connections=NULL;
	__u8* poll_idxs=NULL;
	int poll_count=0;
	int poll_idx=0;

	for (i=0;i<num_eps;i++) {
		int idx=(eps[i]&0x80)?(eps[i]&0x1f)|0x20:eps[i];
		if (!ep_connect[idx]) {
			if (!ep_listener[i]) {server_listen(idx);}
			if (!ep_connect[idx]) poll_count++;
		}
	}
	poll_connections=(struct pollfd*)calloc(poll_count,sizeof(struct pollfd));
	poll_idxs=(__u8*)calloc(poll_count,sizeof(__u8));
	for (i=0;i<num_eps;i++) {
		int idx=(eps[i]&0x80)?(eps[i]&0x1f)|0x20:eps[i];
		if (!ep_connect[idx]) {
			poll_connections[poll_idx].fd=ep_listener[i];
			poll_connections[poll_idx].events=POLLIN;
			poll_idxs[poll_idx]=idx;
			poll_idx++;
		}
	}

	int ep_count=poll_count;
	int rc=-1;
	if (poll(poll_connections,poll_count,timeout)) {
		for (poll_idx=0;i<poll_count;poll_idx++) {
			if (poll_connections[poll_idx].revents==POLLIN) {
				int idx=poll_idxs[poll_idx];
				if (server_connect(idx,0)) {
					ep_buf[idx] = (__u8*) malloc(TCP_BUFFER_SIZE);
					ep_count--;
				}
			}
		}
		rc=ep_count;
	} else {
		rc=ETIMEDOUT;
	}
	free(poll_connections);
	poll_connections=NULL;
	free(poll_idxs);
	poll_idxs=NULL;
	return rc;

}

int TCP_Helper::open_endpoints(__u8* eps,__u8 num_eps,int timeout) {
if (p_server)
	return server_open_endpoints(eps,num_eps,timeout);
else
	return client_open_endpoints(eps,num_eps,timeout);
}

void TCP_Helper::reset() {
	
}

bool TCP_Helper::is_connected() {
	return p_is_connected;
}

