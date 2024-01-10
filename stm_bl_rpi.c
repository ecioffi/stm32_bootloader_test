
// TODO: split up into four? files: test_prog.c test_prog.h raspi.h bl.h ?

// AN4286 has figures
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <linux/gpio.h>

#ifndef STM_BL_DEBUG
#define STM_BL_DEBUG
#endif
#include "stm_bl_base.h"

const char* spi_device = "/dev/spidev0.0";
const char* gpio_device = "/dev/gpiochip4";
const int spi_delay = 10; // microseconds
const int spi_hz_max = 2000000;

const uint32_t application_address=0x08000000;
const uint32_t FLASH_SIZE=512000;

typedef enum
{
    BOOT0_PIN    = 3,
    NRST_PIN     = 4,
    SPI_CS_PIN   = 8,
    SPI_MISO_PIN = 9,
    SPI_MOSI_PIN = 10,
    SPI_CLK_PIN  = 11
} RASPI_PIN;

int spi_fd=-1, gpio_fd=-1;
bool in_bootloader = false;
VERBOSITY verbose = VERBOSITY_QUIET;

int8_t stm_swap_byte_(const uint8_t send)
{
    static uint8_t recv;
    static struct spi_ioc_transfer tr = {
		.rx_buf = (uint64_t) &recv,
		.len = 1,
		.delay_usecs = spi_delay,
		.speed_hz = spi_hz_max,
		.bits_per_word = 8,
	};
    tr.tx_buf = (uint64_t) &send;
    
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) == -1)
    {
        printf("Unable to write to spi via ioctl: %s\n", strerror(errno));
        exit(RET_HOST_ERR);
    }
    return recv;
}

uint8_t stm_swap_byte(const uint8_t send)
{
    uint8_t recv = stm_swap_byte_(send);
    if (verbose) printf("Sent to SPI: 0x%02X. Read back from SPI: 0x%02X.\n", send, recv);
    return recv;
}

uint8_t stm_recv_byte_() { return stm_swap_byte_(DUMMY_BYTE); }

uint8_t stm_recv_byte()
{
    uint8_t recv = stm_recv_byte_();
    if (verbose) printf("Read back from SPI: 0x%02X.\n", recv);
    return recv;
}

void stm_recv_bytes_into(uint8_t* recv_buff, int len)
{
    char recv_str[2048];
    const int olm=20; //one line max bytes

    for (int i = 0; i < len; i++)
    {
        recv_buff[i] = stm_recv_byte_();
        char c0 = len>olm && i%32==0 ? '\n' : ' ';
        sprintf(recv_str+i*3, "%c%02X", c0, recv_buff[i]);
    }

    char c1 = strlen(recv_str)>olm ? '\n' : ' ';
    printf("Read back from SPI:%c0x %s.\n", c1, recv_str);
}

/* Reads memory starting from a given address and ending after given length.
   Reads are broken up into given chunk size---
   (max 256 although there is no bounds checking, so behavior outside would be undefined.)
   Read data is copied into specified buffer.
*/
STM_BL_RET rt_read_memory(uint32_t addr, uint8_t* buff, int len, int chunk_sz)
{
    printf("Routine: Read memory beginning...\n");
    STM_BL_RET ret=RET_OK;

    for (uint32_t i=0, end_addr=addr+len; addr<end_addr; i++, addr+=chunk_sz, buff+=chunk_sz)
    {
        printf("LOOP ITERATION: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%08X\n\n", addr, addr+chunk_sz);
        if (addr + chunk_sz > end_addr)
        {
            chunk_sz = end_addr - addr;
        }
        if ((ret = stm_bl_cmd_read(addr, buff, chunk_sz-1)) != RET_OK)
        {
            return ret;
        }
    }

    printf("Routine: Read memory done OK.\n\n");
    return RET_OK;
}

// same params as read above
STM_BL_RET rt_write_memory(uint32_t addr, uint8_t* buff, int len, int chunk_sz)
{
    printf("Routine: Write memory beginning...\n");
    STM_BL_RET ret=RET_OK;

    for (uint32_t i=0, end_addr=addr+len; addr<end_addr; i++, addr+=chunk_sz, buff+=chunk_sz)
    {
        printf("LOOP ITERATION: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%02X \n\n", addr, addr+chunk_sz);
        if (addr + chunk_sz > end_addr)
        {
            chunk_sz = end_addr - addr;
        }
        if ((ret = stm_bl_cmd_write(addr, buff, chunk_sz-1)) != RET_OK)
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
STM_BL_RET rt_erase_write_verify(uint32_t addr, uint8_t* buff, int len, int chunk_sz)
{
    printf("Routine: erase_write_and_verify_flash memory beginning...\n");
    STM_BL_RET ret=RET_OK;
    
    if ((ret=stm_bl_cmd_erase_global())!=RET_OK) return ret;
    
    //uint8_t dumbbuff[256];
    //if ((ret=stm_bl_bl_cmd_get(dumbbuff))!=RET_OK) return ret;
    
    //wait_msg(10);

    for (uint32_t i=0, end_addr=addr+len; addr<end_addr; i++, addr+=chunk_sz, buff+=chunk_sz)
    {
        printf("LOOP ITERATION: %i\n", i);
        printf("BEGIN ADDRESS 0x%08X ||| END ADDRESS 0x%08X \n\n", addr, addr+chunk_sz);
        
        if (addr + chunk_sz > end_addr) chunk_sz=end_addr-addr;

        if ((ret = stm_bl_cmd_write(addr, buff, chunk_sz-1)) != RET_OK) return ret;

        static uint8_t cmp_buff[256];
        if ((ret = stm_bl_cmd_read(addr, cmp_buff, chunk_sz-1)) != RET_OK) return ret;

        if (memcmp(buff, cmp_buff, chunk_sz)!=0) return RET_ERR;
    }

    printf("Routine: erase_write_and_verify_flash memory done OK.\n");
    return RET_OK;
}

void gpio_write(int pin, bool value)
{
    struct gpio_v2_line_request rq = {
        .offsets[0]=pin,
        .consumer="stmbl",
        .num_lines=1,
        .config= {
            .flags=GPIO_V2_LINE_FLAG_OUTPUT
    }};
    struct gpio_v2_line_values lv = {.bits=value, .mask=1};

    if (ioctl(gpio_fd, GPIO_V2_GET_LINE_IOCTL, &rq) == -1 || rq.fd<0)
    {
        printf("Unable to get line handle from ioctl for pin %d: %s\n", pin, strerror(errno));
        exit(RET_HOST_ERR);
    }

    int code = ioctl(rq.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);
    close(rq.fd);
    if (code == -1)
    {
        printf("Unable to set line value using ioctl for pin %d: %s\n", pin, strerror(errno));
        exit(RET_HOST_ERR);
    }
}

void gpio_reset(int pin)
{
    struct gpio_v2_line_request rq = {
        .offsets[0]=pin,
        .consumer="stmbl",
        .num_lines=1
    };
    struct gpio_v2_line_config config = {.flags=4, .num_attrs=0};

    if (ioctl(gpio_fd, GPIO_V2_GET_LINE_IOCTL, &rq) == -1 || rq.fd<0)
    {
        printf("Unable to get line handle from ioctl for pin %d: %s\n", pin, strerror(errno));
        exit(RET_HOST_ERR);
    }

    int code = ioctl(rq.fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &config);
    close(rq.fd);
    if (code == -1)
    {
        printf("Unable to set line config using ioctl for pin %d: %s\n", pin, strerror(errno));
        exit(RET_HOST_ERR);
    }
}

void restart_STM()
{
    printf("setting reset-pin low...");
    gpio_write(NRST_PIN, 0);
    printf("done\n");

    usleep(100000);

    printf("setting reset-pin high...");
    gpio_write(NRST_PIN, 1);
    printf("done\n");

    sleep(1);
}

void configure_host()
{
    RASPI_PIN SPI_PINS[] = {SPI_CS_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CLK_PIN};
    char cmd[100] = {0};
    for (int i=0; i<sizeof(SPI_PINS)/sizeof(RASPI_PIN); i++)
    {
        sprintf(cmd, "pinctrl set %d a0", SPI_PINS[i]);
        system(cmd);
    }

    if ((spi_fd = open(spi_device, O_RDWR)) < 0)
    {
        printf("Unable to open spi device %s: %s\n", spi_device, strerror(errno));
        exit(RET_HOST_ERR);
    }

    int mode = SPI_MODE_0;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE32, &mode) == -1)
    {
        printf("Unabled to set SPI_MODE_0: %s\n", strerror(errno));
        exit(RET_HOST_ERR);
    }

    if ((gpio_fd = open(gpio_device, O_RDONLY)) < 0)
    {
        printf("Unabled to open gpio device %s: %s\n", gpio_device, strerror(errno));
        exit(RET_HOST_ERR);
    }

    struct gpiochip_info info;
    struct gpio_v2_line_info line_info = {.offset=8}; // cs0 gpio pin

    if (ioctl(gpio_fd, GPIO_V2_GET_LINEINFO_IOCTL, &line_info) == -1)
    {
        printf("Unable to get line info from offset %d: %s\n", line_info.offset, strerror(errno));
        exit(RET_ERR);
    }

    if (strcmp("spi0 CS0", line_info.consumer) != 0)
    {
        printf("PIN%d consumer '%s' != '%s'; gpio dev sanity check failed!\n");
        exit(RET_ERR);
    }
}

void enter_bootloader()
{
    printf("setting boot0-pin high...");
    gpio_write(BOOT0_PIN, 1);
    printf("done\n");
    sleep(1);

    restart_STM();
    in_bootloader=true;
}

void exit_bootloader()
{
    printf("setting boot0-pin low...");
    gpio_write(BOOT0_PIN, 0);
    printf("done\n");
    sleep(1);

    restart_STM();
    //gpio_reset(SPI_CS_PIN);
    gpio_reset(SPI_MISO_PIN);
    gpio_reset(SPI_MOSI_PIN);
    gpio_reset(SPI_CLK_PIN);
    in_bootloader=false;
}

const int def_chunk_size=256;
uint8_t* global_buff=NULL;
char* filename=NULL;

void cleanup()
{
    if (in_bootloader) exit_bootloader();
    printf("cleaning up and exiting...\n");
    if (spi_fd!=-1) close(spi_fd);
    if (gpio_fd!=-1) close(gpio_fd);

    if (filename) free(filename);
    if (global_buff) free(global_buff);
}

// TODO: add test functions; sanitize length input maybe?
// ideas for tests: Get rt loop; erase and verify, write random flash and verify; 
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

    configure_host();
    enter_bootloader();
    STM_BL_RET ret = stm_bl_sync();
    if (ret!=RET_OK)
    {
        printf("Bootloader sync failed with error: %s\n\n", STM_BL_RET_STR[ret]);
        exit(ret);
    }

    global_buff = malloc(FLASH_SIZE);
    switch (rt)
    {
        case RT_SYNC:
            // do nothing since we're already sync'd
            break;
        case RT_GET_CMD:
            ret=stm_bl_cmd_get(global_buff);
            break;
        case RT_ERASE_CMD:
            ret=stm_bl_cmd_erase_global();
            break;
        case RT_READ_CMD:
            ret = stm_bl_cmd_read(address, global_buff, chunk_sz-1);
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

    printf("Routine executed with ret code: %s\n\n", STM_BL_RET_STR[ret]);
    return ret;
}