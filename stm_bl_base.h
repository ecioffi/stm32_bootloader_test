#ifndef STMBL_BASE_H__
#define STMBL_BASE_H__

#include <stdint.h>

#ifdef STM_BL_DEBUG
typedef enum
{
    VERBOSITY_QUIET,     // almost nothing
    VERBOSITY_SEND_RECV, // send/recv data but not poll req
    VERBOSITY_MAX        // everything
} VERBOSITY;
#endif

typedef enum 
{
    DUMMY_BYTE = 0xA5,
    FRAME_BYTE = 0x5A,
    ACK_BYTE   = 0x79,
    NACK_BYTE  = 0x1F,
    ZERO_BYTE  = 0x00
} MAGIC_BYTES;

typedef enum
{
    GET_CMD   = 0x00,
    READ_CMD  = 0x11,
    WRITE_CMD = 0x31,
    ERASE_CMD = 0x44
} STM_BL_CMD_CODE;

typedef enum
{
    RET_OK,
    RET_UNEXPECTED_BYTE,
    RET_NACK,
    RET_TIMEOUT,
    RET_ERR,
    RET_HOST_ERR
} STM_BL_RET;

#ifdef STM_BL_DEBUG
static const char *STM_BL_RET_STR[] =
{
    "RET_OK",
    "RET_UNEXPECTED_BYTE",
    "RET_NACK",
    "RET_TIMEOUT",
    "RET_ERR",
    "RET_HOST_ERR"
};
#endif

void       stm_bl_read_dfn_into(uint8_t* recv_buff, int len);
void       stm_bl_read_df_into(uint8_t* recv_buff);
uint8_t    stm_bl_send_bytes(const uint8_t* send_buff, int len);
STM_BL_RET stm_bl_send_until_recv(uint8_t send, uint8_t target);
STM_BL_RET stm_bl_get_ack();
STM_BL_RET stm_bl_send_bytes_xor_ack_seeded(const uint8_t* buff, int len, uint8_t xor);
STM_BL_RET stm_bl_send_bytes_xor_ack(const uint8_t* buff, int len);
STM_BL_RET stm_bl_send_byte_xor_ack(const uint8_t byte);
STM_BL_RET stm_bl_send_cmd_header_ack(const uint8_t cmd_code);
STM_BL_RET stm_bl_sync();
STM_BL_RET stm_bl_cmd_get(uint8_t* buff);
STM_BL_RET stm_bl_cmd_read(uint32_t address, uint8_t* buff, uint8_t len_m1);
STM_BL_RET stm_bl_cmd_write(uint32_t address, uint8_t* buff, uint8_t len_m1);
// enum STM_BL_RET bl_cmd_erase() // TODO: implement address/page based erase
STM_BL_RET stm_bl_cmd_erase_global();

#endif