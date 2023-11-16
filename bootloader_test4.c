
// TODO: split up into four? files: test_prog.c test_prog.h raspi.h bl.h ?

// AN4286 has figures
#include <bcm2835.h> //for swap bytes w/ rasberry pi
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>

const int bcm_delay = 20; // microseconds
const int poll_timeout = 4096;
const uint16_t spi_clk_div = BCM2835_SPI_CLOCK_DIVIDER_128; //BCM2835_SPI_CLOCK_DIVIDER_65536;

enum RASPI_PINS
{
    BOOT0_PIN    = 3,
    NRST_PIN     = 4,
    SPI_CS_PIN   = 8,
    SPI_MISO_PIN = 9,
    SPI_MOSI_PIN = 10,
    SPI_CLK_PIN  = 11
};

enum MAGIC_BYTES
{
    DUMMY_BYTE = 0xA5,
    FRAME_BYTE = 0x5A,
    ACK_BYTE   = 0x79,
    NACK_BYTE  = 0x1F,
    ZERO_BYTE  = 0x00
};

enum CMD_CODES
{
    GET_CMD_CODE   = 0x00,
    READ_CMD_CODE  = 0x11,
    WRITE_CMD_CODE = 0x31,
    ERASE_CMD_CODE = 0x44
};

enum VERBOSITY
{
    VERBOSITY_QUIET,     // almost nothing
    VERBOSITY_SEND_RECV, // send/recv data but not poll req
    VERBOSITY_MAX        // everything
};

const uint32_t application_address=0x08000000;
const uint32_t FLASH_SIZE=512000;

bool in_bootloader = false;
bool spi_setup = false;
enum VERBOSITY verbose = VERBOSITY_QUIET;

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

// uint8_t swap_byte_sel_print(const uint8_t send)
// {
//     uint8_t recv = swap_byte_(send);
//     if (recv != DUMMY_BYTE && recv != 0 && verbose)
//     {
//         printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X.\n", send, recv);
//     }

//     return recv;
// }

void recv_bytes_into(uint8_t* recv_buff, int len)
{
    char recv_str[2048];
    const int olm=20; //one line max bytes

    for (int i = 0; i < len; i++)
    {
        recv_buff[i] = recv_byte_();
        char c0 = len>olm && i%32==0 ? '\n' : ' ';
        sprintf(recv_str+i*3, "%c%02X", c0, recv_buff[i]);
    }

    char c1 = strlen(recv_str)>olm ? '\n' : ' ';
    printf("Read back from SPI:%c0x %s.\n", c1, recv_str);
}

/* sends a dummy byte to initiate reception of data frame of pre-known length n */
void read_dfn_into(uint8_t* recv_buff, int len)
{
    swap_byte(DUMMY_BYTE);
    recv_bytes_into(recv_buff, len);
}

/* sends a dummy byte to initiate a data frame of unknown length.
   The first byte received is the remaining length of the df minus 1 */
void bl_read_df_into(uint8_t* recv_buff)
{
    swap_byte(DUMMY_BYTE);
    int len = swap_byte(DUMMY_BYTE)+1;
    //printf("bl_read_df_into: len=%i\n", len);
    recv_bytes_into(recv_buff, len);
}

/* send len number of bytes from buff and returns the xor of all sent bytes */
uint8_t send_bytes(const uint8_t* send_buff, int len)
{
    char send_str[2048];
    char recv_str[2048]={0};
    char* recv_str_ = recv_str+1;
    uint8_t xor = 0;
    const int olm=20; //one line max bytes

    for (int i = 0; i < len; i++)
    {
        uint8_t recv = swap_byte_(send_buff[i]);
        xor ^= send_buff[i];
        
        char c0 = len>olm && i%32==0 ? '\n' : ' ';
        sprintf(send_str+i*3, "%c%02X", c0, send_buff[i]);

        if (recv != DUMMY_BYTE)
        {
            recv_str_+=sprintf(recv_str_, "%02X ", recv);
        }
        else
        {      
            if (*(recv_str_-1)!='x') *(recv_str_++)='x';
        }
    }

    char c1 = len>olm? '\n' : ' ';
    char c2 = strlen(recv_str)>olm? '\n' : ' ';
    printf("Sent to SPI:%c0x%s.%c", c1,send_str,c1);
    printf("Read back from SPI:%c0x %s.\n", c2,recv_str+1);
    
    return xor;
}

/* sends specified byte until target or NACK byte is received.
   OR if we timeout that RET_TIMEOUT is returned. Timeout limit set above.
 */
enum RET_CODE send_until_recv(uint8_t send, uint8_t target)
{
    for (int t = 0; t < poll_timeout; t++)
    {
        uint8_t recv = verbose>=VERBOSITY_MAX ? swap_byte(send) : swap_byte_(send);
        
        /* check for target first in case we are polling for dummy byte */
        if (recv == target)
        {
            printf("Poll complete [%i]--> 0x%02X.\n", t+1,recv);
            return RET_OK;
        }
        
        if (recv == DUMMY_BYTE) continue;

        return recv==NACK_BYTE ? RET_NACK : RET_UNEXPECTED_BYTE;
    }

    return RET_TIMEOUT;
}

enum RET_CODE bl_get_ack()
{
    enum RET_CODE ret;

    // do we really need to check for dummy bytes here? STM basically always sends dummies
    // and if we don't get the ACK we will catch that anyway
    if ( swap_byte(ZERO_BYTE) != DUMMY_BYTE ) { return RET_UNEXPECTED_BYTE; }

    if ( (ret = send_until_recv(DUMMY_BYTE, ACK_BYTE) ) != RET_OK) { return ret; }

    return swap_byte(ACK_BYTE)==DUMMY_BYTE ? RET_OK : RET_UNEXPECTED_BYTE;
}

/* Sends len number of bytes and then the checksum of the data immediately after;
   (xor checksum is seeded with the xor parameter passed.)
   Ack is then gotten.
*/
enum RET_CODE bl_send_bytes_xor_ack_seeded(const uint8_t* buff, int len, uint8_t xor)
{
    // printf("bl_send_bytes_xor_ack_seeded: len=%i\n", len);
    xor ^= send_bytes(buff, len);
    swap_byte(xor);
    return bl_get_ack();
}

/* Sends len number of bytes and then the checksum of the data immediately after;
   Ack of xor checksum is then gotten
*/
enum RET_CODE bl_send_bytes_xor_ack(const uint8_t* buff, int len)
{
    return bl_send_bytes_xor_ack_seeded(buff, len, 0x00); // start xor at zero
}

/* Send byte with its checksum and get ack after
*/
enum RET_CODE bl_send_byte_xor_ack(const uint8_t byte)
{
    return bl_send_bytes_xor_ack_seeded(&byte, 1, 0xFF); // start xor at 0xFF for one byte
}

/* Sends a command header consisting of the FRAME_BYTE, command code, and its checksum.
   After, acknowledgment is gotten.
*/
enum RET_CODE bl_send_cmd_header_ack(const uint8_t cmd_code)
{
    swap_byte(FRAME_BYTE);
    return bl_send_byte_xor_ack(cmd_code);
}


enum RET_CODE bl_sync()
{
    printf("Attempting bootloader sync procedure...\n");
    enum RET_CODE ret;

    if ( (ret = send_until_recv(FRAME_BYTE, DUMMY_BYTE) ) != RET_OK) { return ret; }
    if ( (ret = bl_get_ack())  != RET_OK) { return ret; }

    printf("Bootloader sync done OK.\n\n");
    return RET_OK;
}

enum RET_CODE bl_cmd_get(uint8_t* buff)
{
    printf("Command: Get 0x%02X beginning...\n", GET_CMD_CODE);
    enum RET_CODE ret;
    
    if ( (ret = bl_send_cmd_header_ack(GET_CMD_CODE) ) != RET_OK) { return ret; }

    bl_read_df_into(buff);

    if ((ret = bl_get_ack()) != RET_OK) { return ret; } // step6

    printf("Command: Get 0x%02X done OK.\n\n", GET_CMD_CODE);
    return RET_OK;
}

enum RET_CODE bl_cmd_read(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Read 0x%02X beginning...\n", READ_CMD_CODE);
    enum RET_CODE ret;

    if ( (ret = bl_send_cmd_header_ack(READ_CMD_CODE) ) != RET_OK) { return ret; }

    uint32_t addr_ = __builtin_bswap32(address); // to get bytes in expected endianness order
    if ( (ret = bl_send_bytes_xor_ack((uint8_t*) &addr_, sizeof(addr_)) ) != RET_OK) { return ret; }

    if ( (ret = bl_send_byte_xor_ack(len_m1)) != RET_OK) { return ret; }

    swap_byte(DUMMY_BYTE);
    recv_bytes_into(buff, len_m1 + 1);

    printf("Command: Read 0x%02X done OK.\n\n", READ_CMD_CODE);
    return RET_OK;
}

enum RET_CODE bl_cmd_write(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Write 0x%02X beginning...\n", WRITE_CMD_CODE);
    enum RET_CODE ret;

    if ( (ret = bl_send_cmd_header_ack(WRITE_CMD_CODE) ) != RET_OK) { return ret; }

    uint32_t addr_ = __builtin_bswap32(address); // to get bytes in expected order
    if ( (ret = bl_send_bytes_xor_ack((uint8_t*) &addr_, sizeof(addr_)) ) != RET_OK) { return ret; }

    // step 7-- send data frame --number of bytes to be written (1byte) & checksum (1byte)
    swap_byte(len_m1);
    printf("cmd_write: sending buff...\n");
    bl_send_bytes_xor_ack_seeded(buff, len_m1+1, len_m1);

    printf("Command: Write 0x%02X done OK.\n\n", WRITE_CMD_CODE);
    return RET_OK;
}

// TODO: implement address/page based erase
enum RET_CODE bl_cmd_erase()
{
    return RET_OK;
}

enum RET_CODE bl_cmd_erase_global()
{
    printf("Command: Erase memory 0x%02X beginning...\n", ERASE_CMD_CODE);
    enum RET_CODE ret;

    if ( (ret = bl_send_cmd_header_ack(ERASE_CMD_CODE) ) != RET_OK) { return ret; }
    
    /* for some reason with erase we need to send the code without checksum then ACK */
    uint8_t SPECIAL_ERASE_CODE[] = {0xFF, 0xFF};
    uint8_t xor = send_bytes(SPECIAL_ERASE_CODE, sizeof(SPECIAL_ERASE_CODE));
    bl_get_ack();
    
    /* checksum sent and second ACK performed in second stage */
    swap_byte(xor);
    bl_get_ack();


    printf("Command: Erase memory 0x%02X done OK.\n\n", ERASE_CMD_CODE);
    return RET_OK;
}

/* Reads memory starting from a given address and ending after given length.
   Reads are broken up into given chunk size---
   (max 256 although there is no bounds checking, so behavior outside would be undefined.)
   Read data is copied into specified buffer.
*/
enum RET_CODE rt_read_memory(uint32_t addr, uint8_t* buff, int len, int chunk_sz)
{
    printf("Routine: Read memory beginning...\n");
    enum RET_CODE ret=RET_OK;

    for (uint32_t i=0, end_addr=addr+len; addr<end_addr; i++, addr+=chunk_sz, buff+=chunk_sz)
    {
        printf("LOOP ITERATION: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%08X\n\n", addr, addr+chunk_sz);
        if (addr + chunk_sz > end_addr)
        {
            chunk_sz = end_addr - addr;
        }
        if ((ret = bl_cmd_read(addr, buff, chunk_sz-1)) != RET_OK)
        {
            return ret;
        }
    }

    printf("Routine: Read memory done OK.\n\n");
    return RET_OK;
}

// same params as read above
enum RET_CODE rt_write_memory(uint32_t addr, uint8_t* buff, int len, int chunk_sz)
{
    printf("Routine: Write memory beginning...\n");
    enum RET_CODE ret=RET_OK;

    for (uint32_t i=0, end_addr=addr+len; addr<end_addr; i++, addr+=chunk_sz, buff+=chunk_sz)
    {
        printf("LOOP ITERATION: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%02X \n\n", addr, addr+chunk_sz);
        if (addr + chunk_sz > end_addr)
        {
            chunk_sz = end_addr - addr;
        }
        if ((ret = bl_cmd_write(addr, buff, chunk_sz-1)) != RET_OK)
        {
            return ret;
        }
    }

    printf("Routine: Write memory done OK.\n\n");
    return ret;
}

// void wait_msg(int sec)
// {
//     printf("waiting for ~%i seconds", sec);
//     for (int i=0; i<sec*2; i++)
//     {
//         putchar('.');
//         fflush(stdout);
//         bcm2835_delay(500);
//     }
//     printf("done\n");
// }

// same params as read/write cmd/rt
enum RET_CODE rt_erase_write_verify(uint32_t addr, uint8_t* buff, int len, int chunk_sz)
{
    printf("Routine: erase_write_and_verify_flash memory beginning...\n");
    enum RET_CODE ret=RET_OK;
    
    if ((ret=bl_cmd_erase_global())!=RET_OK) return ret;
    
    //uint8_t dumbbuff[256];
    //if ((ret=bl_cmd_get(dumbbuff))!=RET_OK) return ret;
    
    //wait_msg(10);

    for (uint32_t i=0, end_addr=addr+len; addr<end_addr; i++, addr+=chunk_sz, buff+=chunk_sz)
    {
        printf("LOOP ITERATION: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%08X \n\n", addr, addr+chunk_sz);
        
        if (addr + chunk_sz > end_addr) chunk_sz=end_addr-addr;

        if ((ret = bl_cmd_write(addr, buff, chunk_sz-1)) != RET_OK) return ret;

        static uint8_t cmp_buff[256];
        if ((ret = bl_cmd_read(addr, cmp_buff, chunk_sz-1)) != RET_OK) return ret;

        if (memcmp(buff, cmp_buff, chunk_sz)!=0) return RET_ERROR;
    }

    printf("Routine: erase_write_and_verify_flash memory done OK.\n");
    return RET_OK;
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

    // The defaults besides clock divider
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(spi_clk_div);
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);

    bcm2835_gpio_fsel(BOOT0_PIN, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(NRST_PIN, BCM2835_GPIO_FSEL_OUTP);

    const enum RASPI_PINS SPI_PINS[] = { SPI_CS_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CLK_PIN };
    for (int i=0; i<sizeof(SPI_PINS)/sizeof(SPI_PINS[0]); i++)
    {
        //printf("setting SPI_PINS[%i] --- %i\n", i, SPI_PINS[i]);
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

const int def_chunk_size=256;
uint8_t* global_buff=NULL;
char* filename=NULL;

void cleanup()
{
    if (in_bootloader) exit_bootloader();
    if (spi_setup) cleanup_spi();

    if (filename) free(filename);
    if (global_buff) free(global_buff);
}

// TODO: add test functions; sanitize length input maybe?
// ideads for tests: Get rt loop; erase and verify, write random flash and verify; 
int main(int argc, char **argv)
{
    atexit(cleanup);

    enum ROUTINE {
        NO_RT,
        OPT_VERBOSE='v',
        OPT_CHUNK_SZ='c',
        OPT_ADDRESS='a',
        OPT_LENGTH='l',
        RT_GET_CMD='g',
        RT_READ_CMD='r',
        RT_ERASE_CMD='e',
        RT_READ='R',
        RT_WRITE='w',
        RT_SYNC='s',
        RT_ER_WT_VR='z',
        RT_TEST='t',
        RT_HELP='h'
    };
    
    struct option long_options[] = {
        {"verbose",            optional_argument, 0, OPT_VERBOSE  },
        {"chunk-size",         required_argument, 0, OPT_CHUNK_SZ },
        {"address",            required_argument, 0, OPT_ADDRESS  },
        {"length",             required_argument, 0, OPT_LENGTH   },
        {"get-cmd",            no_argument,       0, RT_GET_CMD   },
        {"read-cmd",           optional_argument, 0, RT_READ_CMD  },
        {"erase-cmd",          no_argument,       0, RT_ERASE_CMD },
        {"read",               optional_argument, 0, RT_READ      },
        {"write",              required_argument, 0, RT_WRITE     },
        {"sync",               no_argument,       0, RT_SYNC      },
        {"erase-write-verify", required_argument, 0, RT_ER_WT_VR  },
        {"test",               no_argument,       0, RT_TEST      },
        {"help",               no_argument,       0, RT_HELP      },
        {0,0,0,0},
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
    enum ROUTINE rt = NO_RT;
    int chunk_sz=def_chunk_size;
    uint32_t address=application_address;
    uint32_t length=FLASH_SIZE;
    
    while ((c = getopt_long(argc, argv, "vc:a:l:gr::eR::w:sz:th", long_options, 0)) != -1 && c!=RT_HELP)
    {
        switch (c)
        {
            case OPT_VERBOSE:
                verbose= OPT_ARG_KLUDGE ? atoi(optarg) : VERBOSITY_SEND_RECV;
                if (verbose>VERBOSITY_MAX) verbose=VERBOSITY_MAX;
                if (verbose<VERBOSITY_QUIET) verbose=VERBOSITY_QUIET;
                break;
            case OPT_CHUNK_SZ:
                chunk_sz=atoi(optarg);
                if (chunk_sz>256) chunk_sz=256;
                if (chunk_sz<1) chunk_sz=1;
                break;
            case OPT_ADDRESS:
                if (optarg) address=strtol(optarg, NULL, 0);
                break;
            case OPT_LENGTH:
                if (optarg) length=strtol(optarg, NULL, 0);
                break;
            case RT_READ_CMD:
                if (OPT_ARG_KLUDGE) address=strtol(optarg, NULL, 0);
                rt = (rt==NO_RT)? c : RT_HELP;
                break;
            case RT_READ:
            case RT_WRITE:
            case RT_ER_WT_VR:
                if (OPT_ARG_KLUDGE) filename=strdup(optarg);
                rt = (rt==NO_RT)? c : RT_HELP;
                break;
            case RT_SYNC:
            case RT_TEST:
            case RT_GET_CMD:
            case RT_ERASE_CMD:
                rt = (rt==NO_RT)? c : RT_HELP;
                break;
            case RT_HELP:
            default:
                rt = RT_HELP;
        }
    }

    // TODO bounds checking for length? what would happen if we tried to go out of bounds anyway?

    if (rt==NO_RT || rt==RT_HELP)
    {
        printf("**********Raspberry Pi-->STM32 Bootloader Driver Utility v0.9**********\n");
        printf("Supported routines:\n"
                "\t-g | --get-cmd\n"
                "\t-r | --read-cmd\n"
                "\t-e | --erase-cmd\n"
                "\t-R | --read           [file]\n"
                "\t-w | --write          [opt. file]\n"
                "\t-s | --sync\n"
                "\t-t | --test\n"
                "\t-h | --help\n\n"
                "Supported options:\n"
                "\t-v | --verbose        [def. 1; w/o flag: 0; 0,1,2 OK]\n"
                "\t-c | --chunk-size     [def. %i]\n"
                "\t-a | --address        [def. 0x%08X]\n"
                "\t-l | --length         [def. %i]\n",
                
                def_chunk_size,
                application_address,
                FLASH_SIZE
        );
        return RET_OK;
    }

    configure_spi();
    enter_bootloader();
    enum RET_CODE ret = bl_sync();
    if (ret!=RET_OK)
    {
        printf("Bootloader sync failed with error: %s\n\n", RET_CODE_STR[ret]);
        exit(ret);
    }

    global_buff = malloc(FLASH_SIZE);
    switch (rt)
    {
        case RT_SYNC:
            // do nothing since we're already sync'd
            break;
        case RT_GET_CMD:
            ret=bl_cmd_get(global_buff);
            break;
        case RT_ERASE_CMD:
            ret=bl_cmd_erase_global();
            break;
        case RT_READ_CMD:
            ret = bl_cmd_read(address, global_buff, chunk_sz-1);
            break;
        case RT_READ:
            printf("filename: %s\n", filename);
            ret = rt_read_memory(address, global_buff, length, chunk_sz);
            if (filename)
            {
                FILE *fptr = fopen(filename, "w");
                fwrite(global_buff, 1, length, fptr);
                fclose(fptr);
            }
            break;
        case RT_WRITE:
        case RT_ER_WT_VR:
        {
            FILE *fptr = fopen(filename, "r");
            size_t len = fread(global_buff, 1, FLASH_SIZE, fptr);
            ret = rt==RT_WRITE ? 
                rt_write_memory(address, global_buff, len, chunk_sz) :
                rt_erase_write_verify(address, global_buff, len, chunk_sz);
            fclose(fptr);
            break;
        }
        case RT_TEST:
            printf("tests not implemented yet.\n");
        default:
            break; //should not get here
    }

    printf("Routine executed with ret code: %s\n\n", RET_CODE_STR[ret]);
    return ret;
}