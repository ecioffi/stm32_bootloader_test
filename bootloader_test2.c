
//AN4286 has figures 
#include <bcm2835.h>
#include <stdio.h>
#include <stdlib.h>

const uint8_t NRST_PIN  = 4;
const uint8_t BOOT0_PIN = 3;

const uint8_t DUMMY_BYTE = 0xA5;
const uint8_t FRAME_BYTE = 0x5A;
const uint8_t ACK_BYTE   = 0x79;
const uint8_t NACK_BYTE  = 0x1F;
const uint8_t ZERO_BYTE  = 0x00;

#define FLASH_SIZE 512000

enum RETURN_CODE
{
    OK,
    UNEXPECTED_BYTE,
    NACK
};

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
    //bcm2835_st_delay(0, 15);
    return recv;
}

uint8_t swap_byte_selective_print(uint8_t send)
{
    uint8_t recv = bcm2835_spi_transfer(send);
    if (recv != DUMMY_BYTE)
    {
		printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X.\n", send, recv);
	}
    //bcm2835_st_delay(0, 15);
    return recv;
}

/* 
todo: fix return/buffer stuff
void swap_bytesn(char* buf, uint32_t len)
{
    char * intialBuf = buf;
    bcm2835_spi_transfern(buf, len);
    printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X..\n", (uint32_t*)intialBuf, buf);
    bcm2835_st_delay(0, 20);
}
*/


// DM00081379 figure 3
int bootloader_sync()
{
    printf("attempting bootloader sync procedure...\n");
	uint8_t recv;
	
	/* send sync bytes until we get the right response */
	while ( (recv=swap_byte(FRAME_BYTE)) != DUMMY_BYTE ) { }
	
	if ( swap_byte(ZERO_BYTE) != DUMMY_BYTE )
	{
		//printf("error: expected dummy byte\n");
		return UNEXPECTED_BYTE;
	}

	while ( (recv=swap_byte_selective_print(DUMMY_BYTE)) != ACK_BYTE && recv != NACK_BYTE ) { }
	
	
	if ( (recv=swap_byte(ACK_BYTE) != DUMMY_BYTE) )
	{
		//printf("error: expected dummy byte\n");
		return UNEXPECTED_BYTE;
	}
	else
	{
		//printf("Bootloader sync successful\n");
		return OK;
	}
    
    return 1;
}

int bootloader_cmd_test()
{
	const uint8_t CMD_CODE = 0x00;
	const uint8_t CMD_XOR  = CMD_CODE^0xFF;
	uint8_t recv;
	
	swap_byte(FRAME_BYTE);
	swap_byte(CMD_CODE);
	while ( (recv=swap_byte(CMD_XOR)) != ACK_BYTE) {} //polling on step3 of fig. 4
    //if (recv != ACK_BYTE)
    //{
	//	printf("failed on step3\n");
	//	return 1;
	//}
	
	swap_byte(ZERO_BYTE);
	//recv=swap_byte(ZERO_BYTE);
	while ( (recv=swap_byte(ZERO_BYTE)) != ACK_BYTE) {} //polling on step5
    /*if (recv != ACK_BYTE)
    {
		printf("failed on step5\n");
		return 1;
	}*/
    swap_byte(ACK_BYTE); // step6 
    recv=swap_byte(DUMMY_BYTE);
    if (recv!=DUMMY_BYTE) //step6.1 to read data sent by the slave, the master must first send a dummy byte (called BUSY BYTE)
    {
		printf("failed on step6.1\n");
		return 1;
	}

	while ( (recv=swap_byte(ZERO_BYTE)) != DUMMY_BYTE ) { } //RECEIVING DATA
	printf("Command issued successfully\n");
    return 0;
}

/*
 * NOTE:
 * If the write destination is the flash memory, the master must wait enough time for the 
 * sentbuffer to be written (refer to product datasheet for timing values) before polling 
 * 
 */
int bootloader_cmd_write_memory(){
	printf("Command: Write memory 0x31 beginning\n");
	//const uint8_t CMD_CODE = 0x31;
	//const uint8_t CMD_XOR  = CMD_CODE^0xFF;	//unused variables
	//uint8_t recv;
	
	// swap_byte(FRAME_BYTE);  // step1-start of frame 
	return 0;
}

int bootloader_cmd_read_memory_helper(uint8_t* buff, uint32_t* cur_address, int* index){
	printf("Command: Read memory 0x11 beginning\n");
	const uint8_t CMD_CODE = 0x11;
	const uint8_t CMD_XOR  = CMD_CODE^0xFF;
	uint8_t recv;
	
	swap_byte(FRAME_BYTE);  // step1-start of frame
	swap_byte(CMD_CODE);  // step2-send command 
	swap_byte(CMD_XOR);
	
	while( ( recv=swap_byte(ZERO_BYTE) ) != ACK_BYTE  && recv != NACK_BYTE) {//step3 wait for ack or nack
		if(recv == NACK_BYTE){
			printf("CMD READ FAILED ON STEP 4\n"
			);
			return -1;
		 } 
	}
	swap_byte(ACK_BYTE);
	printf("STEP 4 received ack... %x\n", recv );

	
	uint8_t read_address[4] = {}; 
	read_address[0] = (*cur_address >> 24) & 0xFF;
	read_address[1] = (*cur_address >> 16) & 0xFF;
	read_address[2] = (*cur_address >> 8) & 0xFF;
	read_address[3] = (*cur_address) & 0xFF;
	uint8_t read_address_xor = 0x00;
	for(int i = 0; i < sizeof(read_address); i++) {  //step5 - send data frame (start address)
		swap_byte(read_address[i]);
		read_address_xor ^=  read_address[i];
	}
	//swap_bytesn( (char *) read_address, sizeof(read_address) );
	swap_byte(read_address_xor);
	
	while( ( recv=swap_byte_selective_print(DUMMY_BYTE) ) != ACK_BYTE && recv != NACK_BYTE) {//step5 wait for ack or nack
		if(recv == NACK_BYTE){
			printf("CMD READ FAILED ON STEP 5\n");
			return -1;
		 } 
	}
	printf("STEP 6 received ack/nack... %x\n", recv );
	swap_byte(ACK_BYTE);
	
	//send data frame --number of bytes to be read (1byte) & checksum (1byte)
	uint8_t num_bytes = 255;
	uint8_t num_bytes_xor = num_bytes ^ 0xFF;
	swap_byte(num_bytes);
	swap_byte(num_bytes_xor);
	
	//receive aack
	while( ( recv=swap_byte_selective_print(ZERO_BYTE) ) != ACK_BYTE  && recv != NACK_BYTE) {
		if(recv == NACK_BYTE){
			printf("CMD READ FAILED ON STEP 7\n");
			return -1;
		} 
	}
	swap_byte(ACK_BYTE);
	if (swap_byte(DUMMY_BYTE) != DUMMY_BYTE)
	{
	    printf("CMD READ FAILED ON STEP 8; expected dummy byte\n");
	    return -1;
	}
	
	//receive data
	printf("DATA READ BEGIN...\n");
	for(int i = 0; i < num_bytes; i++){
		recv=swap_byte(ZERO_BYTE);
		/* do not look for nack byte in data read
		if(recv == NACK_BYTE){
			printf("CMD READ FAILED ON STEP 8\n");
			return -1;
		} 
		**/
		buff[(*index)] = recv;
		*index +=1;
	}
	printf("DATA READ END\n");

	
	printf("WHAT WAS READ FROM STM MEMORY\t");
	for(int i = (*index) - num_bytes; i < (*index); i++){
		printf("%02X", buff[i]);
	}
	printf("\n");
	*cur_address += num_bytes;
	return 0;
}

int bootloader_cmd_read_memory(uint32_t memory_start, uint32_t length){
	uint32_t cur_address = memory_start;
	uint32_t end_address = memory_start + length;
	uint8_t buff[FLASH_SIZE * 2] = {0};
	int index = 0;
	int outer_loop_iteration = 0;
	
	FILE *fptr = fopen("flash.bin", "w");
	
	
	while(cur_address < end_address){
		printf("OUTER LOOP ITERATON: %i\n", outer_loop_iteration);
		printf("BEGIN ADDRESS %08X ||| END ADDRESS %02X \n\n\n\n", cur_address, end_address);
		if (bootloader_cmd_read_memory_helper(buff, &cur_address, &index)==-1)
		{
		    return -1;
		}
		outer_loop_iteration += 1;
		bootloader_sync();
	 }
	 
	 fwrite(buff, 1, length, fptr);
	 fclose(fptr);
	 return 0;
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
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_128);
    //bcm2835_spi_set_speed_hz(1000);
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);
    
    printf("setting boot0-pin high...");
    bcm2835_gpio_write(BOOT0_PIN, 1);
    printf("done\n");
    bcm2835_delay(100);
    
    restart_STM();
    bootloader_sync();
    //bootloader_cmd_test();
    //bcm2835_delay(2000);
    bootloader_cmd_read_memory(0x08000000, FLASH_SIZE);
    //bootloader_cmd_read_memory();
    
    printf("setting boot0-pin low...");
    bcm2835_gpio_write(BOOT0_PIN, 0);
    printf("done\n");
    
    restart_STM();

    printf("cleaning up and exiting...\n");
    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}
