
// AN4286 has figures
#include <bcm2835.h> //for swap bytes w/ rasberry pi
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>

const int bcm_delay = 20; // microseconds
const int poll_timeout = 2048;
const uint16_t spi_clk_div = BCM2835_SPI_CLOCK_DIVIDER_128; //BCM2835_SPI_CLOCK_DIVIDER_65536;

const uint8_t BOOT0_PIN = 3;
const uint8_t NRST_PIN = 4;
const uint8_t SPI_PINS[4] = {8,9,10,11};

const uint8_t DUMMY_BYTE = 0xA5;
const uint8_t FRAME_BYTE = 0x5A;
const uint8_t ACK_BYTE   = 0x79;
const uint8_t NACK_BYTE  = 0x1F;
const uint8_t ZERO_BYTE  = 0x00;

const uint32_t application_address=0x08000000;
const uint32_t FLASH_SIZE=512000;

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
   The first byte recieved is the remaining length of the df minus 1 */
void read_df_into(uint8_t* recv_buff)
{
    swap_byte(DUMMY_BYTE);
    int len = swap_byte(DUMMY_BYTE)+1;
    //printf("read_df_into: len=%i\n", len);
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

/* sends len number of bytes and then the checksum of the data immediately after;
   xor checksum is seeded with the parameter passed to the function
*/
void send_bytes_xor(const uint8_t* buff, int len, uint8_t xor)
{
    xor ^= send_bytes(buff, len);
    swap_byte(xor);
}

/* sends specified byte until target or NACK byte is recieved */
enum RET_CODE send_until_recv(uint8_t send, uint8_t target)
{
    for (int t = 0; t < poll_timeout; t++)
    {
        uint8_t recv = swap_byte(send);
        
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

    printf("Bootloader sync done OK.\n\n");
    return RET_OK;
}

void send_cmd_header(const uint8_t RT_CODE)
{
    const uint8_t RT_HEADER[3] = {FRAME_BYTE, RT_CODE , RT_CODE^0xFF};
    send_bytes(RT_HEADER, sizeof(RT_HEADER));
}

enum RET_CODE bootloader_cmd_get(uint8_t* buff)
{
    printf("Command: Get 0x00 beginning...\n");
    send_cmd_header(0x00);

    enum RET_CODE ret;
    if ( (ret = bootloader_get_ack() ) != RET_OK) { return ret; }
    read_df_into(buff);
    if ((ret = bootloader_get_ack()) != RET_OK) { return ret; } // step6

    printf("Command: Get 0x00 done OK.\n\n");
    return RET_OK;
}

enum RET_CODE bootloader_cmd_read(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Read 0x11 beginning...\n");
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

    printf("Command: Read 0x11 done OK.\n\n");
    return RET_OK;
}

enum RET_CODE bootloader_cmd_write(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    printf("Command: Write 0x31 beginning...\n");
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

    printf("Command: Write 0x31 done OK.\n\n");
    return RET_OK;
}

enum RET_CODE read_memory(uint32_t address, uint8_t* buff, int len, int chunk)
{
    printf("Routine: Read memory beginning...\n");
    enum RET_CODE ret=RET_OK;

    for (uint32_t i=0, end_address=address+len; address<end_address; i++, address+=chunk, buff+=chunk)
    {
        printf("LOOP ITERATON: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%08X\n\n", address, address+chunk);
        if (address + chunk > end_address)
        {
            chunk = end_address - address;
        }
        if ((ret = bootloader_cmd_read(address, buff, chunk-1)) != RET_OK)
        {
            return ret;
        }
    }

    printf("Routine: Read memory done OK.\n\n");
    return ret;
}

enum RET_CODE write_memory(uint32_t address, uint8_t* buff, int len, int chunk)
{
    printf("Routine: Write memory beginning...\n");
    enum RET_CODE ret=RET_OK;

    for (uint32_t i=0, end_address=address+len; address<end_address; i++, address+=chunk, buff+=chunk)
    {
        printf("LOOP ITERATON: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%02X \n\n", address, address+chunk);
        if (address + chunk > end_address)
        {
            chunk = end_address - address;
        }
        if ((ret = bootloader_cmd_write(address, buff, chunk-1)) != RET_OK)
        {
            return ret;
        }
    }

    printf("Routine: Write memory done OK.\n\n");
    return ret;
}

// TODO: implement erase function
enum RET_CODE bootloader_cmd_erase()
{
    printf("Command: Erase memory 0x44 beginning...\n");
    send_cmd_header(0x31);
    enum RET_CODE ret;

    printf("Command: Erase memory 0x44 done OK.\n\n");
    return RET_OK;
}

enum RET_CODE erase_write_and_verify_flash(uint32_t address, uint8_t* buff, int len, int chunk)
{
    printf("Routine: erase_write_and_verify_flash memory beginning...\n");
    enum RET_CODE ret=RET_OK;
    
    if ((ret=erase_memory())!=RET_OK) return ret;

    for (uint32_t i=0, end_address=address+len; address<end_address; i++, address+=chunk, buff+=chunk)
    {
        printf("LOOP ITERATON: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%08X \n\n", address, address+chunk);
        
        if (address + chunk > end_address) chunk=end_address-address;

        if ((ret = bootloader_cmd_write(address, buff, chunk-1)) != RET_OK) return ret;

        static uint8_t cmp_buff[256];
        if ((ret = bootloader_cmd_read(address, cmp_buff, chunk-1)) != RET_OK) return ret;

        if (memcmp(buff, cmp_buff, chunk)!=0) return RET_ERROR;
    }

    printf("Routine: erase_write_and_verify_flash memory done OK...\n");
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

// TODO: update help output; add test functions; sanitize length input maybe?
// ideads for tests: Get rt loop; erase and verify, write random flash and verify; 
int main(int argc, char **argv)
{
    atexit(cleanup);

    enum ROUTINE {
        NO_RT,
        OPT_VERBOSE='v',
        OPT_CHUNK_SIZE='c',
        OPT_ADDRESS='a',
        OPT_LENGTH='l',
        RT_GET='g',
        RT_READ='r',
        RT_READ_MEMORY='R',
        RT_WRITE_FILE='w',
        RT_SYNC='s',
        RT_TEST='t',
        RT_HELP='h'
    };
    
    struct option long_options[] = {
        {"verbose",     no_argument,       0,  OPT_VERBOSE     },
        {"chunk-size",  required_argument, 0,  OPT_CHUNK_SIZE  },
        {"address",     required_argument, 0,  OPT_ADDRESS     },
        {"length",      required_argument, 0,  OPT_LENGTH      },
        {"get-cmd",     no_argument,       0,  RT_GET         },
        {"read-cmd",    optional_argument, 0,  RT_READ        },
        {"read-memory", optional_argument, 0,  RT_READ_MEMORY },
        {"write-file",  required_argument, 0,  RT_WRITE_FILE  },
        {"sync",        no_argument,       0,  RT_SYNC        },
        {"test",        no_argument,       0,  RT_TEST        },
        {"help",        no_argument,       0,  RT_HELP        },
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
    enum ROUTINE rt = NO_RT;
    int chunk_size=def_chunk_size;
    uint32_t address=application_address;
    uint32_t length=FLASH_SIZE;
    
    while ((c = getopt_long(argc, argv, "vc:a:r::R::w:sthl:g", long_options, 0)) != -1 && c!=RT_HELP)
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
            case RT_READ:
                if (OPT_ARG_KLUDGE) address=strtol(optarg, NULL, 0);
                rt = (rt==NO_RT)? c : RT_HELP;
                break;
            case RT_READ_MEMORY:
                if (OPT_ARG_KLUDGE) filename=strdup(optarg);
                rt = (rt==NO_RT)? c : RT_HELP;
                break;
            case RT_WRITE_FILE:
                filename=strdup(optarg);
                rt = (rt==NO_RT)? c : RT_HELP;
                break;
            case RT_SYNC:
            case RT_TEST:
            case RT_GET:
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
                "\t-R | --read-memory  [file]\n"
                "\t-w | --write-file   [opt. file]\n"
                "\t-s | --sync\n"
                "\t-t | --test\n"
                "\t-h | --help\n\n"
                "Supported options:\n"
                "\t-v | --verbose\n"
                "\t-c | --chunk-size   [def. %i]\n"
                "\t-a | --address      [def. 0x%08X]\n"
                "\t-l | --length       [def. %i]\n",
                def_chunk_size,
                application_address,
                FLASH_SIZE
        );
        return RET_OK;
    }

    configure_spi();
    enter_bootloader();
    enum RET_CODE ret = bootloader_sync();
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
        case RT_READ:
            ret = bootloader_cmd_read(address, global_buff, chunk_size-1);
            break;
        case RT_READ_MEMORY:
            printf("filename: %s\n", filename);
            ret = read_memory(address, global_buff, length, chunk_size);
            if (filename)
            {
                FILE *fptr = fopen(filename, "w");
                fwrite(global_buff, 1, length, fptr);
                fclose(fptr);
            }
            break;
        case RT_WRITE_FILE:
        {
            FILE *fptr = fopen(filename, "r");
            size_t len = fread(global_buff, 1, sizeof(global_buff), fptr);
            ret = write_memory(address, global_buff, len, chunk_size);
            fclose(fptr);
            break;
        }
        case RT_GET:
            ret=bootloader_cmd_get(global_buff);
            break;
        case RT_TEST:
            printf("tests not implemented yet.\n");
        default:
            break; //should not get here
    }

    printf("Routine executed with ret code: %s\n\n", RET_CODE_STR[ret]);
    return ret;
}