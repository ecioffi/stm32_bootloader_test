
// AN4286 has figures
#include <bcm2835.h> //for swap bytes w/ rasberry pi
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>

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

const uint32_t application_address=0x08000000;
#define FLASH_SIZE 512000
// #define RECV_BUFF_SIZE 512
// uint8_t RECV_BUFF[RECV_BUFF_SIZE];

bool in_bootloader = false;
bool spi_setup = false;
bool verbose = false;

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
    if (verbose) printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X.\n", send, recv);
    return recv;
}

uint8_t recv_byte()
{
    uint8_t recv = recv_byte_();
    if (verbose) printf("Read back from SPI: 0x%02X.\n", recv);
    return recv;
}

uint8_t swap_byte_sel_print(const uint8_t send)
{
    uint8_t recv = swap_byte_(send);
    if (recv != DUMMY_BYTE && recv != 0 && verbose)
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

/* send len number of bytes from buff and returns the xor of all sent bytes */
uint8_t send_bytes(const uint8_t* send_buff, int len)
{
    char send_str[1024];
    char recv_str[1024];
    uint8_t xor = 0;
    for (int i = 0; i < len; i++)
    {
        uint8_t recv = swap_byte_(send_buff[i]);
        xor ^= send_buff[i];
        sprintf(send_str + i * 3, "%02X ", send_buff[i]);
        sprintf(recv_str + i * 3, "%02X ", recv);
    }

    printf("Sent to SPI: 0x%s. Read back from SPI: 0x%s.\n", send_str, recv_str);
    return xor;
}

/* sends len number of bytes and then the checksum of the data immediately after;
   xor checksum is seeded with the parameter passed to the function
*/
void send_bytes_xor(const uint8_t* buff, int len, uint8_t xor)
{
    xor ^= send_bytes(buff, len);
    swap_byte(xor);
}

// uint8_t* recv_bytes(int len)
// {
//     if (len > RECV_BUFF_SIZE)
//     {
//         printf("%s: Line %d: Buffer overflow error on \n", __FILE__, __LINE__);
//         exit(1);
//     }

//     recv_bytes_into(RECV_BUFF, len);
//     return RECV_BUFF;
// }

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

    printf("Bootloader sync successful\n");
    return RET_OK;
}

void send_cmd_header(const uint8_t CMD_CODE)
{
    const uint8_t CMD_HEADER[3] = {FRAME_BYTE, CMD_CODE , CMD_CODE^0xFF};
    send_bytes(CMD_HEADER, sizeof(CMD_HEADER));
}

enum RET_CODE bootloader_cmd_get(uint8_t* buff)
{
    printf("attempting bootloader command get procedure...\n");
    send_cmd_header(0x00);

    enum RET_CODE ret;
    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }
    read_df_into(buff);
    if ((ret = bootloader_get_ack()) != RET_OK) { return ret; } // step6

    return RET_OK;
}

enum RET_CODE bootloader_cmd_read_memory(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Read memory 0x11 beginning\n");
    send_cmd_header(0x11);
    enum RET_CODE ret;

    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }

    uint32_t address_endian = __builtin_bswap32(address);
    send_bytes_xor((uint8_t*) &address_endian, sizeof(address_endian), 0);

    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }

    swap_byte(len_m1);
    swap_byte(len_m1 ^ 0xFF);

    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }

    swap_byte(DUMMY_BYTE);
    recv_bytes_into(buff, len_m1 + 1);

    printf("Command: Read memory 0x11 done\n");
    return RET_OK;
}

enum RET_CODE bootloader_cmd_write_memory(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Write memory 0x31 beginning\n");
    send_cmd_header(0x31);
    enum RET_CODE ret;

    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }

    uint32_t address_endian = __builtin_bswap32(address);
    send_bytes_xor((uint8_t*) &address_endian, sizeof(address_endian), 0);

    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }

    // step 7-- send data frame --number of bytes to be written (1byte) & checksum (1byte)
    swap_byte(len_m1);
    send_bytes_xor(buff, len_m1+1, len_m1);

    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }
    //swap_byte(DUMMY_BYTE);

    return RET_OK;
}

enum RET_CODE read_memory(uint32_t address, uint8_t* buff, int len, int chunk)
{
    enum RET_CODE ret=RET_OK;

    for (uint32_t i=0, end_address=address+len; address<end_address; i++, address+=chunk, buff+=chunk)
    {
        printf("LOOP ITERATON: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%02X \n\n", address, address+chunk);
        if (address + chunk > end_address)
        {
            chunk = end_address - address;
        }
        if ((ret = bootloader_cmd_read_memory(address, buff, chunk-1)) != RET_OK)
        {
            return ret;
        }
    }

    return ret;
}

enum RET_CODE write_memory(uint32_t address, uint8_t* buff, int len, int chunk)
{
    enum RET_CODE ret=RET_OK;

    for (uint32_t i=0, end_address=address+len; address<end_address; i++, address+=chunk, buff+=chunk)
    {
        printf("LOOP ITERATON: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%02X \n\n", address, address+chunk);
        if (address + chunk > end_address)
        {
            chunk = end_address - address;
        }
        if ((ret = bootloader_cmd_write_memory(address, buff, chunk-1)) != RET_OK)
        {
            return ret;
        }
    }

    return ret;
}

// TODO: implement erase function
enum RET_CODE erase_memory()
{
    return RET_OK;
}

enum RET_CODE erase_write_and_verify_flash(uint32_t address, uint8_t* buff, int len, int chunk)
{
    enum RET_CODE ret=RET_OK;
    
    if ((ret=erase_memory)!=RET_OK) return ret;

    for (uint32_t i=0, end_address=address+len; address<end_address; i++, address+=chunk, buff+=chunk)
    {
        printf("LOOP ITERATON: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%02X \n\n", address, address+chunk);
        
        if (address + chunk > end_address) chunk=end_address-address;

        if ((ret = bootloader_cmd_write_memory(address, buff, chunk-1)) != RET_OK) return ret;

        static uint8_t cmp_buff[256];
        if ((ret = bootloader_cmd_read_memory(address, cmp_buff, chunk-1)) != RET_OK) return ret;

        if (memcmp(buff, cmp_buff, chunk)!=0) return RET_ERROR;
    }

    return ret;
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
        exit(RET_ERROR);
    }
    if (!bcm2835_spi_begin())
    {
        printf("bcm2835_spi_begin failed. Are you running as root??\n");
        exit(RET_ERROR);
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

    spi_setup=true;
}

void cleanup_spi()
{
    printf("cleaning up and exiting...\n");
    bcm2835_spi_end();
    bcm2835_close();
    spi_setup=false;
}

void enter_bootloader()
{
    printf("setting boot0-pin high...");
    bcm2835_gpio_write(BOOT0_PIN, 1);
    printf("done\n");
    bcm2835_delay(1000);

    restart_STM();
    in_bootloader=true;
}

void exit_bootloader()
{
    printf("setting boot0-pin low...");
    bcm2835_gpio_write(BOOT0_PIN, 0);
    printf("done\n");
    bcm2835_delay(100);

    restart_STM();
    in_bootloader=false;
}

void cleanup()
{
    if (in_bootloader) exit_bootloader();
    if (spi_setup) cleanup_spi();
}

// TODO: update help output; add test functions; sanitize length input maybe?
// ideads for tests: Get cmd loop; erase and verify, write random flash and verify; 
int main(int argc, char **argv)
{
    atexit(cleanup);

    enum COMMAND {
        NO_CMD,
        OPT_VERBOSE='v',
        OPT_CHUNK_SIZE='c',
        OPT_ADDRESS='a',
        OPT_LENGTH='l',
        CMD_GET='g',
        CMD_READ='r',
        CMD_READ_MEMORY='R',
        CMD_WRITE_FILE='w',
        CMD_SYNC='s',
        CMD_TEST='t',
        CMD_HELP='h'
    };
    
    struct option long_options[] = {
        {"verbose",     no_argument,       0,  OPT_VERBOSE     },
        {"chunk-size",  required_argument, 0,  OPT_CHUNK_SIZE  },
        {"address",     required_argument, 0,  OPT_ADDRESS     },
        {"length",      required_argument, 0,  OPT_LENGTH      },
        {"get-cmd",     no_argument,       0,  CMD_GET         },
        {"read-cmd",    optional_argument, 0,  CMD_READ        },
        {"read-memory", optional_argument, 0,  CMD_READ_MEMORY },
        {"write-file",  required_argument, 0,  CMD_WRITE_FILE  },
        {"sync",        no_argument,       0,  CMD_SYNC        },
        {"test",        no_argument,       0,  CMD_TEST        },
        {"help",        no_argument,       0,  CMD_HELP        },
        {0, 0, 0, 0},
    };

    /* optional short options are a GNU extention.
       The default behavior doesn't pick up an arg separated by a space,
       so this macro fixes that
    */
    #define OPT_ARG_KLUDGE \
    ((optarg == NULL && optind < argc && argv[optind][0] != '-') \
     ? (bool) (optarg = argv[optind++]) \
     : (optarg != NULL))

    int c;
    enum COMMAND cmd = NO_CMD;
    int chunk_size=256;
    uint32_t address=application_address;
    uint32_t length=FLASH_SIZE;
    char* filename=NULL;
    while ((c = getopt_long(argc, argv, "vc:a:r::R::w:sthl:g", long_options, 0)) != -1 && c!=CMD_HELP)
    {
        switch (c)
        {
            case OPT_VERBOSE:
                verbose=true;
                break;
            case OPT_CHUNK_SIZE:
                chunk_size=atoi(optarg);
                if (chunk_size>256) chunk_size=256;
                if (chunk_size<1) chunk_size=1;
                break;
            case OPT_ADDRESS:
                if (optarg) address=strtol(optarg, NULL, 0);
                break;
            case OPT_LENGTH:
                if (optarg) length=strtol(optarg, NULL, 0);
                break;
            case CMD_READ:
                if (OPT_ARG_KLUDGE) address=strtol(optarg, NULL, 0);
                cmd = (cmd==NO_CMD)? c : CMD_HELP;
                break;
            case CMD_READ_MEMORY:
                if (OPT_ARG_KLUDGE) filename=strdup(optarg);
                cmd = (cmd==NO_CMD)? c : CMD_HELP;
                break;
            case CMD_WRITE_FILE:
                filename=strdup(optarg);
                cmd = (cmd==NO_CMD)? c : CMD_HELP;
                break;
            case CMD_SYNC:
            case CMD_TEST:
            case CMD_GET:
                cmd = (cmd==NO_CMD)? c : CMD_HELP;
                break;
            case CMD_HELP:
            default:
                cmd = CMD_HELP;
        }
    }

    if (cmd==NO_CMD || cmd==CMD_HELP)
    {
        printf("\n***Raspberry Pi-->STM32 Bootloader Driver Utility v0.9***\n\n");
        printf("Supported commands:\n\n--sync\n--get\n--read-file [file]\n--write-file [file]\n--test\n");
        return RET_OK;
    }

    configure_spi();
    enter_bootloader();
    enum RET_CODE ret = bootloader_sync();
    if (ret!=RET_OK)
    {
        printf("bootloader sync failed with error: %s", RET_CODE_STR[ret]);
        exit(ret);
    }

    uint8_t buff[FLASH_SIZE*2] = {0};
    switch (cmd)
    {
        case CMD_SYNC:
            // do nothing since we're already sync'd
            break;
        case CMD_READ:
            ret = bootloader_cmd_read_memory(address, buff, chunk_size-1);
            break;
        case CMD_READ_MEMORY:
            printf("filename: %s\n", filename);
            ret = read_memory(address, buff, length, chunk_size);
            if (filename)
            {
                FILE *fptr = fopen(filename, "w");
                fwrite(buff, 1, length, fptr);
                fclose(fptr);
            }
            break;
        case CMD_WRITE_FILE:
        {
            FILE *fptr = fopen(filename, "r");
            size_t len = fread(buff, 1, sizeof(buff), fptr);
            ret = write_memory(address, buff, len, chunk_size);
            fclose(fptr);
            break;
        }
        case CMD_GET:
            ret=bootloader_cmd_get(buff);
            break;
        case CMD_TEST:
            printf("tests not implemented yet.\n");
        default:
            break; //should not get here
    }

    if (filename) free(filename);
    printf("command executed with ret code: %s\n", RET_CODE_STR[ret]);
    return ret;
}
