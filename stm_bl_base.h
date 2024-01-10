#ifndef STMBL_BASE_H__
#define STMBL_BASE_H__

#include <stdint.h>

#ifdef STM_BL_DEBUG
enum VERBOSITY
{
    VERBOSITY_QUIET,     // almost nothing
    VERBOSITY_SEND_RECV, // send/recv data but not poll req
    VERBOSITY_MAX        // everything
};
#endif

enum 
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

enum RET_CODE
{
    RET_OK,
    RET_UNEXPECTED_BYTE,
    RET_NACK,
    RET_TIMEOUT,
    RET_ERR,
    RET_HOST_ERR
};

static const char *RET_CODE_STR[] =
{
    "RET_OK",
    "RET_UNEXPECTED_BYTE",
    "RET_NACK",
    "RET_TIMEOUT",
    "RET_ERR",
    "RET_HOST_ERR"
};

static const uint32_t application_address=0x08000000;
static const uint32_t FLASH_SIZE=512000;
static const int      poll_timeout = 4096;

int8_t swap_byte_(const uint8_t send);
uint8_t swap_byte(const uint8_t send);
uint8_t recv_byte_();
uint8_t recv_byte();
void recv_bytes_into(uint8_t* recv_buff, int len);

void read_dfn_into(uint8_t* recv_buff, int len);
void bl_read_df_into(uint8_t* recv_buff);
uint8_t send_bytes(const uint8_t* send_buff, int len);
enum RET_CODE send_until_recv(uint8_t send, uint8_t target);
enum RET_CODE bl_get_ack();
enum RET_CODE bl_send_bytes_xor_ack_seeded(const uint8_t* buff, int len, uint8_t xor);
enum RET_CODE bl_send_bytes_xor_ack(const uint8_t* buff, int len);
enum RET_CODE bl_send_byte_xor_ack(const uint8_t byte);
enum RET_CODE bl_send_cmd_header_ack(const uint8_t cmd_code);
enum RET_CODE bl_sync();
enum RET_CODE bl_cmd_get(uint8_t* buff);
enum RET_CODE bl_cmd_read(uint32_t address, uint8_t* buff, uint8_t len_m1);
enum RET_CODE bl_cmd_write(uint32_t address, uint8_t* buff, uint8_t len_m1);
// enum RET_CODE bl_cmd_erase() // TODO: implement address/page based erase
enum RET_CODE bl_cmd_erase_global();

#endif