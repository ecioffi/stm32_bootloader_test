#!/bin/bash
set -e
clear

gcc -Werror -Wdeclaration-after-statement -D STM_BL_DEBUG -c stm_bl_base.c -o stm_bl_base.o
gcc -Werror -D STM_BL_DEBUG stm_bl_rpi.c stm_bl_base.o -o stm_bl_rpi