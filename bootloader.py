import time
import RPi.GPIO as GPIO
import spidev

# these pins should be in ALT0 mode (I think)
rpi_mosi = 10
rpi_miso = 9
rpi_sclk = 11

# output mode
rpi_nrst = 4
rpi_boot0 = 3

spi = spidev.SpiDev()

def setup_spi():
    spi.open(0, 0) # bus, dev
    spi.max_speed_hz = 10000
    spi.mode = 0

def setup_pins():
    GPIO.setwarnings(False)
    print("setting up pins...", end="")
    GPIO.setmode(GPIO.BCM) # pin ordering scheme
    
    # alt0 mode cannot be set in python agghghhh
    
    # output pins
    GPIO.setup(rpi_nrst, GPIO.OUT, initial=1)
    GPIO.setup(rpi_boot0, GPIO.OUT, initial=0)
    print("done")
    
def restart_STM():
    print("setting reset-pin low...", end="")
    GPIO.output(rpi_nrst, 0)
    print("done")
    time.sleep(0.5)
    print("setting reset-pin high...", end="")
    GPIO.output(rpi_nrst, 1)
    print("done")
    time.sleep(0.5)
    
def bootloader_test():
    sync_byte = 0x5A
    ack_byte = 0x79
    nack_byte = 0x1F
    
    print("setting boot0-pin high...", end="")
    GPIO.output(rpi_boot0, 1)
    print("done")
    
    time.sleep(0.5)
    restart_STM()
    
    print("sending spi-sync seqeunce...", end="")
    rcvd = spi.xfer3([sync_byte])
    print("done. rcvd: ")
    print('[{}]'.format(', '.join(hex(x) for x in rcvd)))
    
    time.sleep(5)
    print("sending spi-sync seqeunce...", end="")
    rcvd = spi.xfer3([0x00, 0xaa, ack_byte])
    print("done. rcvd: ")
    print('[{}]'.format(', '.join(hex(x) for x in rcvd)))
    
#     for i in range(10):
#         for j in range(10):
#             spi.writebytes([sync_byte])
#             if (spi.readbytes(1)[0] == ack_byte):
#                 print("ACK!!!")
#                 break

    
    print("setting boot0-pin low...", end="")
    GPIO.output(rpi_boot0, 0)
    print("done") 

if __name__ == '__main__':
    setup_pins()
    restart_STM()
    #setup_spi()
    
    #bootloader_test()
    
    spi.close()
    print("DONE")