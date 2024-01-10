#ifdef STM_BL_DEBUG
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#endif

#include "stmbl_base.h"

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

    int i;

    for (i = 0; i < len; i++)
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
        uint8_t recv;
        #ifdef STMBL_DEBUG
        extern int verbose;
        recv = verbose>=VERBOSITY_MAX ? swap_byte(send) : swap_byte_(send);
        #else
        recv = swap_byte_(send);
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
// enum RET_CODE bl_cmd_erase()
// {
//     return RET_OK;
// }

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