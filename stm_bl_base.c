#ifdef STM_BL_DEBUG
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#endif

#include "stm_bl_base.h"

const int poll_timeout = 4096;

int8_t  stm_swap_byte_(const uint8_t send);
uint8_t stm_swap_byte(const uint8_t send);
uint8_t stm_recv_byte_();
uint8_t stm_recv_byte();
void    stm_recv_bytes_into(uint8_t* recv_buff, int len);

/* sends a dummy byte to initiate reception of data frame of pre-known length n */
void stm_bl_read_dfn_into(uint8_t* recv_buff, int len)
{
    stm_swap_byte(DUMMY_BYTE);
    stm_recv_bytes_into(recv_buff, len);
}

/* sends a dummy byte to initiate a data frame of unknown length.
   The first byte received is the remaining length of the df minus 1 */
void stm_bl_read_df_into(uint8_t* recv_buff)
{
    int len; //todo char on dsp
    stm_swap_byte(DUMMY_BYTE);
    len = stm_swap_byte(DUMMY_BYTE)+1;
    //printf("stm_bl_read_df_into: len=%i\n", len);
    stm_recv_bytes_into(recv_buff, len);
}

/* send len number of bytes from buff and returns the xor of all sent bytes */
uint8_t stm_bl_send_bytes(const uint8_t* send_buff, int len)
{
    char send_str[2048];
    char recv_str[2048]={0};
    char* recv_str_ = recv_str+1;
    uint8_t xor_val = 0;
    int i;
    
    const int olm=20; //one line max bytes
    char c0;
    char c1;
    char c2;

    for (i = 0; i < len; i++)
    {
        uint8_t recv = stm_swap_byte_(send_buff[i]);
        xor_val ^= send_buff[i];
        
        c0 = len>olm && i%32==0 ? '\n' : ' ';
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

    c1 = len>olm? '\n' : ' ';
    c2 = strlen(recv_str)>olm? '\n' : ' ';
    printf("Sent to SPI:%c0x%s.%c", c1,send_str,c1);
    printf("Read back from SPI:%c0x %s.\n", c2,recv_str+1);
    
    return xor_val;
}

/* sends specified byte until target or NACK byte is received.
   OR if we timeout that RET_TIMEOUT is returned. Timeout limit set above.
 */
STM_BL_RET stm_bl_send_until_recv(uint8_t send, uint8_t target)
{
    for (int t = 0; t < poll_timeout; t++)
    {
        uint8_t recv;
        #ifdef STMBL_DEBUG
        extern int verbose;
        recv = verbose>=VERBOSITY_MAX ? stm_swap_byte(send) : stm_swap_byte_(send);
        #else
        recv = stm_swap_byte_(send);
        #endif
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

STM_BL_RET stm_bl_get_ack()
{
    STM_BL_RET ret;

    // do we really need to check for dummy bytes here? STM basically always sends dummies
    // and if we don't get the ACK we will catch that anyway
    if ( stm_swap_byte(ZERO_BYTE) != DUMMY_BYTE ) { return RET_UNEXPECTED_BYTE; }

    if ( (ret = stm_bl_send_until_recv(DUMMY_BYTE, ACK_BYTE) ) != RET_OK) { return ret; }

    return stm_swap_byte(ACK_BYTE)==DUMMY_BYTE ? RET_OK : RET_UNEXPECTED_BYTE;
}

/* Sends len number of bytes and then the checksum of the data immediately after;
   (xor checksum is seeded with the xor parameter passed.)
   Ack is then gotten.
*/
STM_BL_RET stm_bl_send_bytes_xor_ack_seeded(const uint8_t* buff, int len, uint8_t xor)
{
    // printf("stm_bl_send_bytes_xor_ack_seeded: len=%i\n", len);
    xor ^= stm_bl_send_bytes(buff, len);
    stm_swap_byte(xor);
    return stm_bl_get_ack();
}

/* Sends len number of bytes and then the checksum of the data immediately after;
   Ack of xor checksum is then gotten
*/
STM_BL_RET stm_bl_send_bytes_xor_ack(const uint8_t* buff, int len)
{
    return stm_bl_send_bytes_xor_ack_seeded(buff, len, 0x00); // start xor at zero
}

/* Send byte with its checksum and get ack after
*/
STM_BL_RET stm_bl_send_byte_xor_ack(const uint8_t byte)
{
    return stm_bl_send_bytes_xor_ack_seeded(&byte, 1, 0xFF); // start xor at 0xFF for one byte
}

/* Sends a command header consisting of the FRAME_BYTE, command code, and its checksum.
   After, acknowledgment is gotten.
*/
STM_BL_RET stm_bl_send_cmd_header_ack(const uint8_t cmd_code)
{
    stm_swap_byte(FRAME_BYTE);
    return stm_bl_send_byte_xor_ack(cmd_code);
}


STM_BL_RET stm_bl_sync()
{
    STM_BL_RET ret;
    printf("Attempting bootloader sync procedure...\n");

    if ( (ret = stm_bl_send_until_recv(FRAME_BYTE, DUMMY_BYTE) ) != RET_OK) { return ret; }
    if ( (ret = stm_bl_get_ack())  != RET_OK) { return ret; }

    printf("Bootloader sync done OK.\n\n");
    return RET_OK;
}

STM_BL_RET stm_bl_cmd_get(uint8_t* buff)
{
    STM_BL_RET ret;
    printf("Command: Get 0x%02X beginning...\n", GET_CMD);
    
    if ( (ret = stm_bl_send_cmd_header_ack(GET_CMD) ) != RET_OK) { return ret; }

    stm_bl_read_df_into(buff);

    if ((ret = stm_bl_get_ack()) != RET_OK) { return ret; } // step6

    printf("Command: Get 0x%02X done OK.\n\n", GET_CMD);
    return RET_OK;
}

STM_BL_RET stm_bl_cmd_read(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    STM_BL_RET ret;
    uint32_t addr_;
    printf("Command: Read 0x%02X beginning...\n", READ_CMD);

    if ( (ret = stm_bl_send_cmd_header_ack(READ_CMD) ) != RET_OK) { return ret; }

    //TODO fix for dsp compatibility
    addr_ = __builtin_bswap32(address); // to get bytes in expected endianness order
    if ( (ret = stm_bl_send_bytes_xor_ack((uint8_t*) &addr_, sizeof(addr_)) ) != RET_OK) { return ret; }

    if ( (ret = stm_bl_send_byte_xor_ack(len_m1)) != RET_OK) { return ret; }

    stm_swap_byte(DUMMY_BYTE);
    stm_recv_bytes_into(buff, len_m1 + 1);

    printf("Command: Read 0x%02X done OK.\n\n", READ_CMD);
    return RET_OK;
}

STM_BL_RET stm_bl_cmd_write(uint32_t address, uint8_t* buff, uint8_t len_m1)
{
    STM_BL_RET ret;
    uint32_t addr_;
    printf("Command: Write 0x%02X beginning...\n", WRITE_CMD);

    if ( (ret = stm_bl_send_cmd_header_ack(WRITE_CMD) ) != RET_OK) { return ret; }

    addr_ = __builtin_bswap32(address); // to get bytes in expected order
    if ( (ret = stm_bl_send_bytes_xor_ack((uint8_t*) &addr_, sizeof(addr_)) ) != RET_OK) { return ret; }

    // step 7-- send data frame --number of bytes to be written (1byte) & checksum (1byte)
    stm_swap_byte(len_m1);
    printf("cmd_write: sending buff...\n");
    stm_bl_send_bytes_xor_ack_seeded(buff, len_m1+1, len_m1);

    printf("Command: Write 0x%02X done OK.\n\n", WRITE_CMD);
    return RET_OK;
}

// TODO: implement address/page based erase
// STM_BL_RET bl_cmd_erase()
// {
//     return RET_OK;
// }

STM_BL_RET stm_bl_cmd_erase_global()
{
    STM_BL_RET ret;
    uint8_t xor_val;
    uint8_t SPECIAL_ERASE_CODE[] = {0xFF, 0xFF};
    printf("Command: Erase memory 0x%02X beginning...\n", ERASE_CMD);

    if ( (ret = stm_bl_send_cmd_header_ack(ERASE_CMD) ) != RET_OK) { return ret; }
    
    /* for some reason with erase we need to send the code without checksum then ACK */
    xor_val = stm_bl_send_bytes(SPECIAL_ERASE_CODE, sizeof(SPECIAL_ERASE_CODE));
    stm_bl_get_ack();
    
    /* checksum sent and second ACK performed in second stage */
    stm_swap_byte(xor_val);
    stm_bl_get_ack();


    printf("Command: Erase memory 0x%02X done OK.\n\n", ERASE_CMD);
    return RET_OK;
}