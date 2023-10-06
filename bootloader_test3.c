
// AN4286 has figures
#include <bcm2835.h> //for swap bytes w/ rasberry pi
#include <stdio.h>
#include <stdlib.h>
#include <string.h> //for strcmp for argv comparisons in main

const int bcm_delay = 20; // microseconds
const int poll_timeout = 256;
const uint16_t spi_clk_div = BCM2835_SPI_CLOCK_DIVIDER_128; //BCM2835_SPI_CLOCK_DIVIDER_65536;

const uint8_t BOOT0_PIN = 3;
const uint8_t NRST_PIN = 4;
const uint8_t SPI_PINS[4] = {8,9,10,11};

const uint8_t DUMMY_BYTE = 0xA5;
const uint8_t FRAME_BYTE = 0x5A;
const uint8_t ACK_BYTE = 0x79;
const uint8_t NACK_BYTE = 0x1F;
const uint8_t ZERO_BYTE = 0x00;

#define FLASH_SIZE 512000
#define RECV_BUFF_SIZE 512
uint8_t RECV_BUFF[RECV_BUFF_SIZE];

enum RET_CODE
{
    RET_OK,
    RET_UNEXPECTED_BYTE,
    RET_NACK,
    RET_TIMEOUT,
    RET_ERROR
};

const char *RET_CODE_STR[5] =
{
    "RET_OK",
    "RET_UNEXPECTED_BYTE",
    "RET_NACK",
    "RET_TIMEOUT",
    "RET_ERROR"
};

/*
void LOGGING_FUNCTION(int level, const char* func, const char* file, const char* line, const char* msg, ...)
{
    printf("[DEBUG%d %s:%d:%s]: %s", level, file, line, func);
    printf(msg, ...);
}

#define DEBUG_LOG( level, ... ) \
do \
{ \
    if ( ( level ) <= global_debug_level ) \
    { \
    LOGGING_FUNCTION( ( level ), __func__, __FILE__, __LINE__, __VA_ARGS__ ); \
    } \
} \
while (0)
*/

uint8_t swap_byte_(const uint8_t send)
{
    uint8_t recv = bcm2835_spi_transfer(send);
    bcm2835_st_delay(0, bcm_delay);
    return recv;
}

uint8_t recv_byte_() { return swap_byte_(DUMMY_BYTE); }

uint8_t swap_byte(const uint8_t send)
{
    uint8_t recv = swap_byte_(send);
    if (1)
    {
        printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X.\n", send, recv);
    }

    return recv;
}

uint8_t recv_byte()
{
    uint8_t recv = recv_byte_();
    if (1)
    {
        printf("Read back from SPI: 0x%02X.\n", recv);
    }

    return recv;
}

uint8_t swap_byte_sel_print(const uint8_t send)
{
    uint8_t recv = swap_byte_(send);
    if (recv != DUMMY_BYTE && recv != 0)
    {
        printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X.\n", send, recv);
    }

    return recv;
}

void recv_bytes_into(uint8_t* recv_buff, int len)
{
    char recv_str[1024];

    for (int i = 0; i < len; i++)
    {
        recv_buff[i] = recv_byte_();
        sprintf(recv_str + i * 3, "%02X ", recv_buff[i]);
    }

    printf("Read back from SPI: 0x%s.\n", recv_str);
}

/* sends a dummy byte to initiate reception of data frame of pre-known length n */
void read_dfn_into(uint8_t* recv_buff, int len)
{
    swap_byte(DUMMY_BYTE);
    recv_bytes_into(recv_buff, len);
}

/* sends a dummy byte to initiate a data frame of unknown length.
   The first byte recieved is the remaining length of the df minus 1 */
void read_df_into(uint8_t* recv_buff)
{
    swap_byte(DUMMY_BYTE);
    int len = swap_byte(DUMMY_BYTE)+1;
    printf("read_df_into: len=%i\n", len);
    recv_bytes_into(recv_buff, len);
}

void send_bytes(const uint8_t* send_buff, int len)
{
    char send_str[1024];
    char recv_str[1024];

    for (int i = 0; i < len; i++)
    {
        uint8_t recv = swap_byte_(send_buff[i]);
        sprintf(send_str + i * 3, "%02X ", send_buff[i]);
        sprintf(recv_str + i * 3, "%02X ", recv);
    }

    printf("Sent to SPI: 0x%s. Read back from SPI: 0x%s.\n", send_str, recv_str);
}

uint8_t* recv_bytes(int len)
{
    if (len > RECV_BUFF_SIZE)
    {
        printf("%s: Line %d: Buffer overflow error on \n", __FILE__, __LINE__);
        exit(1);
    }

    recv_bytes_into(RECV_BUFF, len);
    return RECV_BUFF;
}

/* sends specified byte until target or NACK byte is recieved */
enum RET_CODE send_until_recv(uint8_t send, uint8_t target)
{
    for (int t = 0; t < poll_timeout; t++)
    {
        uint8_t recv = swap_byte(send);
        if (recv == target)
        {
            printf("Poll complete--> 0x%02X.\n", recv);
            return RET_OK;
        }
        if (recv == NACK_BYTE)
        {
            return RET_NACK;
        }
        if (recv == ACK_BYTE)
        {
            return RET_UNEXPECTED_BYTE;
        }
    }

    return RET_TIMEOUT;
}

uint8_t get_xor(uint8_t* data, uint8_t len)
{
    uint8_t xor = 0;
    for (int i = 0; i < len; i++) { xor ^= data[i]; }
    return xor;
}

enum RET_CODE bootloader_get_ack()
{
    enum RET_CODE ret;

    // do we really need to check for dummy bytes here? STM basically always sends dummies
    // and if we don't get the ACK we will catch that anyway
    if ( swap_byte(ZERO_BYTE) != DUMMY_BYTE ) { return RET_UNEXPECTED_BYTE; }

    if ( (ret = send_until_recv(DUMMY_BYTE, ACK_BYTE) ) != RET_OK) { return ret; }

    return swap_byte(ACK_BYTE)==DUMMY_BYTE ? RET_OK : RET_UNEXPECTED_BYTE;
}

enum RET_CODE bootloader_sync()
{
    printf("attempting bootloader sync procedure...\n");
    enum RET_CODE ret;

    if ( (ret = send_until_recv(FRAME_BYTE, DUMMY_BYTE) ) != RET_OK) { return ret; }

    if ( (ret = bootloader_get_ack())  != RET_OK) { return ret; }

    // printf("Bootloader sync successful\n");
    return RET_OK;
}

// const uint8_t* get_cmd_header(const uint8_t CMD_CODE)
// {
//     static uint8_t cmd_header[3];
//     cmd_header = {FRAME_BYTE, CMD_CODE, CMD_CODE^0xFF};
// }

enum RET_CODE bootloader_cmd_test(uint8_t* buff)
{
    const uint8_t CMD_CODE = 0x00;
    const uint8_t CMD_HEADER[3] = {FRAME_BYTE, CMD_CODE, CMD_CODE ^ 0xFF};
    enum RET_CODE ret;

    printf("attempting bootloader command test procedure...\n");
    
    send_bytes(CMD_HEADER, sizeof(CMD_HEADER));
    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }
    read_df_into(buff);
    if ((ret = bootloader_get_ack()) != RET_OK) { return ret; } // step6

    return RET_OK;
}

enum RET_CODE bootloader_cmd_read_memory(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Read memory 0x11 beginning\n");
    const uint8_t CMD_CODE = 0x11;
    const uint8_t CMD_XOR  = CMD_CODE^0xFF;
    const uint8_t CMD_HEADER[3] = {  };
    enum RET_CODE ret;

    swap_byte(FRAME_BYTE); // step1-start of frame
    swap_byte(CMD_CODE);   // step2-send command
    swap_byte(CMD_XOR);

    // polling on step 4
    if ((ret = send_until_recv(DUMMY_BYTE, ACK_BYTE)) != RET_OK)
    {
        return ret;
    }
    swap_byte(ACK_BYTE);
    printf("STEP 4 received ack...\n");

    uint8_t address_payload[5];
    *((uint32_t *)&address_payload) = __builtin_bswap32(address);
    address_payload[4] = get_xor((uint8_t* )&address, sizeof(address));
    send_bytes(address_payload, sizeof(address_payload));

    // polling on step 6
    // swap_byte(ZERO_BYTE);
    if ((ret = send_until_recv(DUMMY_BYTE, ACK_BYTE)) != RET_OK)
    {
        return ret;
    }
    swap_byte(ACK_BYTE);
    printf("STEP 6 received ack/nack...\n");

    // send data frame --number of bytes to be read (1byte) & checksum (1byte)
    swap_byte(len_m1);
    swap_byte(len_m1 ^ 0xFF);

    // polling on step 6
    if ((ret = send_until_recv(DUMMY_BYTE, ACK_BYTE)) != RET_OK)
    {
        return ret;
    }
    swap_byte(ACK_BYTE);
    swap_byte(DUMMY_BYTE);

    // receive data
    printf("DATA READ BEGIN...\n");
    for (int i = 0; i < len_m1 + 1; i++)
    {
        buff[i] = swap_byte(ZERO_BYTE);
    }
    printf("DATA READ END\n");

    printf("WHAT WAS READ FROM STM MEMORY:  0x");
    for (int i = 0; i < len_m1 + 1; i++)
    {
        printf("%02X", buff[i]);
    }
    printf("\n");

    return RET_OK;
}

enum RET_CODE bootloader_cmd_write_memory(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Write memory 0x31 beginning\n");
    const uint8_t CMD_CODE = 0x31;
    const uint8_t CMD_XOR = CMD_CODE ^ 0xFF;
    enum RET_CODE ret;

    swap_byte(FRAME_BYTE); // step1-start of frame
    swap_byte(CMD_CODE);   // step2-send command
    swap_byte(CMD_XOR);

    // polling on step 4
    if ((ret = send_until_recv(DUMMY_BYTE, ACK_BYTE)) != RET_OK)
    {
        return ret;
    }
    swap_byte(ACK_BYTE);
    printf("STEP 4 received ack...\n");

    uint8_t address_payload[5];
    *((uint32_t *)&address_payload) = __builtin_bswap32(address);
    address_payload[4] = get_xor((uint8_t* )&address, sizeof(address));
    send_bytes(address_payload, sizeof(address_payload));

    // polling on step 6
    if ((ret = send_until_recv(DUMMY_BYTE, ACK_BYTE)) != RET_OK)
    {
        return ret;
    }
    swap_byte(ACK_BYTE);
    printf("STEP 6 received ack/nack...\n");

    // step 7-- send data frame --number of bytes to be written (1byte) & checksum (1byte)
    uint8_t len_and_data_xor = len_m1;
    swap_byte(len_m1);
    for (int i = 0; i < len_m1 + 1; i++)
    {
        swap_byte(buff[i]);
        len_and_data_xor ^= buff[i];
    }
    swap_byte(len_and_data_xor);

    // polling on step 8
    if ((ret = send_until_recv(DUMMY_BYTE, ACK_BYTE)) != RET_OK)
    {
        return ret;
    }
    swap_byte(ACK_BYTE);
    swap_byte(DUMMY_BYTE);

    return RET_OK;
}

enum RET_CODE read_memory(uint32_t memory_start, uint32_t length)
{
    uint32_t cur_address = memory_start;
    uint32_t end_address = memory_start + length;
    uint8_t buff[FLASH_SIZE * 2] = {0};
    int index = 0;
    int outer_loop_iteration = 0;
    int read_chunck_size = 256;
    enum RET_CODE ret;

    while (cur_address < end_address)
    {
        printf("OUTER LOOP ITERATON: %i\n", outer_loop_iteration);
        printf("BEGIN ADDRESS %08X ||| END ADDRESS %02X \n\n\n\n", cur_address, end_address);
        if (cur_address + read_chunck_size > end_address)
        {
            read_chunck_size = end_address - cur_address;
        }

        if ((ret = bootloader_cmd_read_memory(cur_address, &buff[index], read_chunck_size - 1)) != RET_OK)
        {
            return ret;
        }
        outer_loop_iteration += 1;
        index += read_chunck_size;
        cur_address += read_chunck_size;
    }
    FILE *fptr = fopen("flash.bin", "w");
    fwrite(buff, 1, length, fptr);
    fclose(fptr);
    return 0;
}

enum RET_CODE write_memory(uint32_t memory_start, char *filename)
{
    uint32_t cur_address = memory_start;
    uint8_t buff[FLASH_SIZE * 2] = {0};
    FILE *fptr = fopen(filename, "r");
    size_t file_size_bytes = fread(buff, sizeof(uint8_t), sizeof(buff), fptr);
    fclose(fptr);
    uint32_t end_address = memory_start + file_size_bytes;

    int index = 0;
    int outer_loop_iteration = 0;
    int write_chunck_size = 256;
    int ret_val;

    while (cur_address < end_address)
    {
        printf("OUTER LOOP ITERATON: %i\n", outer_loop_iteration);
        printf("BEGIN ADDRESS %08X ||| END ADDRESS %02X \n\n\n\n", cur_address, end_address);
        if (cur_address + write_chunck_size > end_address)
        {
            write_chunck_size = end_address - cur_address;
        }

        if ((ret_val = bootloader_cmd_write_memory(cur_address, &buff[index], write_chunck_size - 1)) != RET_OK)
        {
            return ret_val;
        }
        outer_loop_iteration += 1;
        index += write_chunck_size;
        cur_address += write_chunck_size;
    }
    return 0;
}

void restart_STM()
{
    printf("setting reset-pin low...");
    bcm2835_gpio_write(NRST_PIN, 0);
    printf("done\n");

    bcm2835_delay(100);

    printf("setting reset-pin high...");
    bcm2835_gpio_write(NRST_PIN, 1);
    printf("done\n");

    bcm2835_delay(1000);
}

void configure_spi()
{
    if (!bcm2835_init())
    {
        printf("bcm2835_init failed. Are you running as root??\n");
        exit(1);
    }
    if (!bcm2835_spi_begin())
    {
        printf("bcm2835_spi_begin failed. Are you running as root??\n");
        exit(1);
    }

    // The defaults
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(spi_clk_div);
    // bcm2835_spi_set_speed_hz(1000);
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);

    bcm2835_gpio_fsel(BOOT0_PIN, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(NRST_PIN, BCM2835_GPIO_FSEL_OUTP);
    for (int i=0; i<=sizeof(SPI_PINS); i++)
    {
        bcm2835_gpio_fsel(SPI_PINS[i], BCM2835_GPIO_FSEL_ALT0);
    }
}

void cleanup_spi()
{
    printf("cleaning up and exiting...\n");
    bcm2835_spi_end();
    bcm2835_close();
}

void enter_bootloader()
{
    printf("setting boot0-pin high...");
    bcm2835_gpio_write(BOOT0_PIN, 1);
    printf("done\n");
    bcm2835_delay(1000);

    restart_STM();
}

void exit_bootloader()
{
    printf("setting boot0-pin low...");
    bcm2835_gpio_write(BOOT0_PIN, 0);
    printf("done\n");
    bcm2835_delay(100);

    restart_STM();
}

int main(int argc, char **argv)
{
    enum RET_CODE ret = RET_OK;
    uint8_t buff[256] = {0};

    if (argc==1 ||
        strcmp(argv[1], "help")==0 ||
        strcmp(argv[1], "-h")==0 ||
        strcmp(argv[1], "--help")==0 ||
        strcmp(argv[1], "version")==0 ||
        strcmp(argv[1], "-v")==0 ||
        strcmp(argv[1], "--version")==0
    )
    {
        printf("***Raspberry Pi-->STM32 Bootloader Driver Utility v0.9***\n\n");
        printf("Supported commands:sync\n\nget\nread_file (file)\nwrite_file [file]\ntest\n");
        return ret;
    }
    else
    {
        configure_spi();
        enter_bootloader();

        if (strcmp(argv[1], "sync") == 0)
        {

        }
        if (strcmp(argv[1], "get") == 0)
        {
            ret = bootloader_cmd_test(buff);
            printf("get: %s\n", RET_CODE_STR[ret]);
        }
        else if (strcmp(argv[1], "read") == 0)
        {

            enter_bootloader();
            ret = read_memory(0x08000000, FLASH_SIZE);
            printf("READ MEMORY: %s\n", RET_CODE_STR[ret]);
        }
        else if (strcmp(argv[1], "write") == 0)
        {
            if (argc<3) re
            enter_bootloader();
            ret = write_memory(0x08000000, "blink-g0b.bin");
            
        }
        else if (strcmp(argv[1], "test") == 0)
        {
            for (int i = 0; i < 100; i++)
            {
                ret = bootloader_cmd_test(buff);
                printf("[%d] gets: %s\n", i, RET_CODE_STR[ret]);
                if (ret != RET_OK) break;
            }
        }
        else
        {
            printf("cmd '%s' not recognized\n", argv[1]);
        }
    }

    if ( (ret=bootloader_sync()) != RET_OK )
    {
        printf("ERROR: Cannot enter bootloader cmd loop: %s\n", RET_CODE_STR[ret]);
    }
    
    exit_bootloader();
    cleanup_spi();
    return ret;
}
