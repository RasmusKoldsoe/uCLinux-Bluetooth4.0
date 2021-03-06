/*
 * main.c
 *
 *  Created on: Sep 12, 2013
 *      Author: rasmus
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/resource.h>

#include "Parser/HCI_defs.h"
#include "ble_device.h"
#include "dev_tools.h"
#include "Queue/Queue.h"
#include "NetworkStat/NetworkStatistics.h"
#include "SerialLogic/serialLogic.h"
#include "Parser/Parser.h"
#include "COMparser/COMparser.h"
#include "GPIO/gpio_api.h"

void register_sig_handler();
void sigint_handler(int sig);

int stop = 0;

int reset_gpio(int pin)
{
//init GPIO:
	if( !gpio_export(pin) ) {
		printf("ERROR: Exporting gpio port: %d\n", pin);
		return 0;
	}

	if( !gpio_export(LED_IND3) ) {
		printf("ERROR: Exporting gpio port: %d\n", BLE_0_RESET);
		return 0;
	}

	if( !gpio_setDirection(LED_IND3, GPIO_DIR_OUT) ) {
		printf("ERROR: Set gpio port direction.\n");
		return 0;
	}

	if( !gpio_setValue(LED_IND3, 0) ) {
		printf("ERROR: Exporting gpio port.");
		return 0;
	}

//set direction 1=in 0=out
	if( !gpio_setDirection(pin, GPIO_DIR_OUT) ) {
		printf("ERROR: Set gpio port direction\n");
		return 0;
	}
//set GPIO value low:
	if( !gpio_setValue(pin, 0) ) {
		printf("ERROR: Set gpio port state low\n");
		return 0;
	}
//set GPIO value high:
	if( !gpio_setValue(pin, 1) ) {
		printf("ERROR: Set gpio port state high\n");
		return 0;
	}
// Time between setting low and high is measured to ~900us. Minimum reset_n
// low duration according to datasheet is min 1 us.
	return 1;
}




void run_app(BLE_Central_t *bleCentral)
{
	sleep(1);
	datagram_t datagram;
	BLE_Peripheral_t *dev;
// Init Device
	memset(&datagram, 0, sizeof(&datagram));
	get_GAP_DeviceInit(&datagram);
	enqueue(&bleCentral->txQueue, &datagram);
	sleep(1);

// Establish Link
	memset(&datagram, 0, sizeof(datagram));
	char FreqKeyfobMAC[] = {0x78, 0xc5, 0xe5, 0xa0, 0x14, 0x12}; // Freq keyfob address: 78:c5:e5:a0:14:12
	char CC2541KeyFobMAC[] = {0xBC, 0x6A, 0x29, 0xAB, 0x18, 0xD8}; 
	get_GAP_EstablishLinkRequest(&datagram, CC2541KeyFobMAC);
	enqueue(&bleCentral->txQueue, &datagram);
sleep(2);
	get_GAP_EstablishLinkRequest(&datagram, FreqKeyfobMAC);
	enqueue(&bleCentral->txQueue, &datagram);
	sleep(2);

// Sign up for freq service
	dev = findDeviceByMAC(bleCentral, CC2541KeyFobMAC);
	if(dev->_connected) {
		memset(&datagram, 0, sizeof(datagram));
		char d[] = {0x00, 0x01};
		get_GATT_WriteCharValue(&datagram, dev->connHandle, 0x0048, d, 2);
		enqueue(&bleCentral->txQueue, &datagram);
	}

	dev = findDeviceByMAC(bleCentral, FreqKeyfobMAC);
	if(dev->_connected) {
		memset(&datagram, 0, sizeof(datagram));
		char d[] = {0x00, 0x01};
		get_GATT_WriteCharValue(&datagram, dev->connHandle, 0x004D, d, 2);
		enqueue(&bleCentral->txQueue, &datagram);
	}

	if( !gpio_setValue(LED_IND3, 1) ) {
		printf("ERROR: Exporting gpio port.");
		return 0;
	}
	//char letter;
	//scanf("%c", &letter);
	while(!stop)
		usleep(500000);

	if( !gpio_setValue(LED_IND3, 0) ) {
		printf("ERROR: Exporting gpio port.");
		return 0;
	}

// Sign off freq service
	dev = findDeviceByMAC(bleCentral, FreqKeyfobMAC);
	if(dev->_connected) {
		memset(&datagram, 0, sizeof(datagram));
		char d[] = {0x00, 0x00};
		get_GATT_WriteCharValue(&datagram, dev->connHandle, 0x004D, d, sizeof(d));
		enqueue(&bleCentral->txQueue, &datagram);
	}

	dev = findDeviceByMAC(bleCentral, CC2541KeyFobMAC);
	if(dev->_connected) {
		memset(&datagram, 0, sizeof(datagram));
		char d[] = {0x00, 0x00};
		get_GATT_WriteCharValue(&datagram, dev->connHandle, 0x0048, d, sizeof(d)); // 0x4D00
		enqueue(&bleCentral->txQueue, &datagram);
	}

// Terminate Link
	dev = findDeviceByMAC(bleCentral, CC2541KeyFobMAC);
	if(dev->_connected) {
		memset(&datagram, 0, sizeof(datagram));
		get_GAP_TerminateLinkRequest(&datagram, dev->connHandle);
		enqueue(&bleCentral->txQueue, &datagram);
		sleep(2);
	}
	else {
		debug(1, "NO CONNECTION TO DEVICE CC2541KeyFob\n");
	}

	dev = findDeviceByMAC(bleCentral, FreqKeyfobMAC);
	if(dev->_connected) {
		memset(&datagram, 0, sizeof(datagram));
		get_GAP_TerminateLinkRequest(&datagram, dev->connHandle);
		enqueue(&bleCentral->txQueue, &datagram);
		sleep(2);
	}
	else {
		debug(1, "NO CONNECTION TO DEVICE FreqKeyFob\n");
	}

	bleCentral->_run = 0;
	sleep(1);
}




int main (void)
{
	if( !reset_gpio(BLE_0_RESET) )
		return -1;
	debug(2, "Device is reset successfully\n");

	int rt, wt, rct;
	pthread_t read_thread, write_thread, readCOM_thread;
	pthread_attr_t thread_attr;
	BLE_Central_t bleCentral;
	memset(&bleCentral, 0, sizeof(BLE_Central_t));

	bleCentral.port = "/dev/ttyS4";
	bleCentral._run = 1;
	bleCentral.rxQueue = queueCreate();
	bleCentral.txQueue = queueCreate();
	bleCentral.fd = open_serial(bleCentral.port, O_RDWR);
	if(bleCentral.fd < 0){
		printf("ERROR Opening port: %d\n", bleCentral.fd);
		return -1;
	}

		//Prepare mapped mem.
	//bleCentral.mapped_mem=preparemappedMem("ble");
	//bleCentral.rt_count = 0;

	//Register signal handler.
	register_sig_handler(); 

	pthread_attr_init(&thread_attr);
	pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

	rt = pthread_create( &read_thread, &thread_attr, read_serial, (void *)&bleCentral);
	if( rt ) {
	  printf("ERROR read thread: Return code from pthread_create() is %d\n", rt);
	  return -1;
	}

	wt = pthread_create( &write_thread, &thread_attr, write_serial, (void *)&bleCentral);
	if( wt ) {
	  printf("ERROR write thread: Return code from pthread_create() is %d\n", wt);
	  return -1;
	}

	rct = pthread_create( &readCOM_thread, &thread_attr, RxComParser, (void *)&bleCentral);
	if( rct ) {
	  printf("ERROR readCOM thread: Return code from pthread_create() is %d\n", rct);
	  return -1;
	}

	while(bleCentral._run) {
		run_app(&bleCentral);
	}

	pthread_join(rct, NULL);
	pthread_join(rt, NULL);
	pthread_join(wt, NULL);

	queueDestroy(&bleCentral.txQueue);
	queueDestroy(&bleCentral.rxQueue);
	close_serial(bleCentral.fd);

	printNetworkStat();

	printf("Exiting main thread\n");
	pthread_exit(NULL);
}

void register_sig_handler()
{
	struct sigaction sia;

	bzero(&sia, sizeof sia);
	sia.sa_handler = sigint_handler;

	if (sigaction(SIGINT, &sia, NULL) < 0) {
		perror("sigaction(SIGINT)");
		exit(1);
	} 
}

void sigint_handler(int sig)
{
	stop = 1;
}
