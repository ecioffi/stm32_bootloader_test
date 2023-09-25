
// spi.c
//
// Example program for bcm2835 library
// Shows how to interface with SPI to transfer a byte to and from an SPI device
//
// After installing bcm2835, you can build this 
// with something like:
// gcc -o spi spi.c -l bcm2835
// sudo ./spi
//
// Or you can test it before installing with:
// gcc -o spi -I ../../src ../../src/bcm2835.c spi.c
// sudo ./spi
//
// Author: Mike McCauley
// Copyright (C) 2012 Mike McCauley
// $Id: RF22.h,v 1.21 2012/05/30 01:51:25 mikem Exp $
 
#include <bcm2835.h>
#include <stdio.h>

const uint8_t NRST_PIN = 4;
const uint8_t BOOT0_PIN = 3;

void restart_STM()
{
    printf("setting reset-pin low...");
    bcm2835_gpio_write(NRST_PIN, 0);
    printf("done\n");
    
    bcm2835_delay(1000);
    
    printf("setting reset-pin high...");
    bcm2835_gpio_write(NRST_PIN, 1);
    printf("done\n");
    
    //bcm2835_delay(100);
}

uint8_t swap_byte(uint8_t send)
{
    uint8_t recv = bcm2835_spi_transfer(send);
    printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X.\n", send, recv);
    bcm2835_st_delay(0, 8);
    return recv;
}

int bootloader_test()
{
    const uint8_t SYNC_BYTE_SEND = 0x5A;
    const uint8_t SYNC_BYTE_RECV = 0xA5;
    const uint8_t ACK_BYTE = 0x79;
    const uint8_t NACK_BYTE = 0x1F;
    const uint8_t ZERO_BYTE = 0x00;
    
    for (int i=0; i<1; i++)
    {
    
        uint8_t recv;
        
        /* send sync bytes until we get the right response */
        while ( (recv=swap_byte(SYNC_BYTE_SEND)) != SYNC_BYTE_RECV ) { }
        
        uint8_t dummy_byte;
        
        while ( (dummy_byte=swap_byte(ZERO_BYTE)) ==/* SYNC_BYTE_RECV || dummy_byte==*/ NACK_BYTE || dummy_byte==ZERO_BYTE ) { }
        //while ( (dummy_byte=swap_byte(ZERO_BYTE)) == SYNC_BYTE_RECV || dummy_byte== NACK_BYTE || dummy_byte==ZERO_BYTE ) { }
        
        while ( (recv=swap_byte(dummy_byte)) != ACK_BYTE ) { }
        
        recv=swap_byte(ACK_BYTE);
        if (recv != dummy_byte)
        {
            printf("ugh2\n");
            continue;
        }
        else
        {
            printf("hallelujah!\n");
            return 0;
        }
    }
    
    return 1;
}
 
int main(int argc, char **argv)
{
    // If you call this, it will not actually access the GPIO Use for testing
	//        bcm2835_set_debug(1);
 
    if (!bcm2835_init())
    {
      printf("bcm2835_init failed. Are you running as root??\n");
      return 1;
    }
 
    if (!bcm2835_spi_begin())
    {
      printf("bcm2835_spi_begin failed. Are you running as root??\n");
      return 1;
    }
    
    // The defaults
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);      
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_32768);
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);
    
    printf("setting boot0-pin high...");
    bcm2835_gpio_write(BOOT0_PIN, 1);
    printf("done\n");
    bcm2835_delay(10);
    
    restart_STM();
    bootloader_test();
    
    printf("setting boot0-pin low...");
    bcm2835_gpio_write(BOOT0_PIN, 0);
    printf("done\n");
    
    restart_STM();

    printf("cleaning up and exiting...\n");
    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}
