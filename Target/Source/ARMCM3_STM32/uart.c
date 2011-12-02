/****************************************************************************************
|  Description: bootloader UART communication interface source file
|    File Name: uart.c
|
|----------------------------------------------------------------------------------------
|                          C O P Y R I G H T
|----------------------------------------------------------------------------------------
|   Copyright (c) 2011  by Feaser    http://www.feaser.com    All rights reserved
|
|----------------------------------------------------------------------------------------
|                            L I C E N S E
|----------------------------------------------------------------------------------------
| This file is part of OpenBLT. OpenBLT is free software: you can redistribute it and/or
| modify it under the terms of the GNU General Public License as published by the Free
| Software Foundation, either version 3 of the License, or (at your option) any later
| version.
|
| OpenBLT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
| without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
| PURPOSE. See the GNU General Public License for more details.
|
| You should have received a copy of the GNU General Public License along with OpenBLT.
| If not, see <http://www.gnu.org/licenses/>.
|
| A special exception to the GPL is included to allow you to distribute a combined work 
| that includes OpenBLT without being obliged to provide the source code for any 
| proprietary components. The exception text is included at the bottom of the license
| file <license.html>.
| 
****************************************************************************************/

/****************************************************************************************
* Include files
****************************************************************************************/
#include "boot.h"                                /* bootloader generic header          */


#if (BOOT_COM_UART_ENABLE > 0)
/****************************************************************************************
* Type definitions
****************************************************************************************/
typedef struct
{
  volatile blt_int16u SR;                             /* status register               */
  blt_int16u          RESERVED0;
  volatile blt_int16u DR;                             /* data register                 */
  blt_int16u          RESERVED1;
  volatile blt_int16u BRR;                            /* baudrate register             */
  blt_int16u          RESERVED2;
  volatile blt_int16u CR1;                            /* control register 1            */
  blt_int16u          RESERVED3;
  volatile blt_int16u CR2;                            /* control register 2            */
  blt_int16u          RESERVED4;
  volatile blt_int16u CR3;                            /* control register 3            */
  blt_int16u          RESERVED5;
  volatile blt_int16u GTPR;                           /* guard time and prescale reg.  */
  blt_int16u          RESERVED6;
} tUartRegs;                                          /* UART register layout type     */


/****************************************************************************************
* Macro definitions
****************************************************************************************/
#define UART_BIT_UE    ((blt_int16u)0x2000)           /* USART enable bit              */
#define UART_BIT_TE    ((blt_int16u)0x0008)           /* transmitter enable bit        */
#define UART_BIT_RE    ((blt_int16u)0x0004)           /* receiver enable bit           */
#define UART_BIT_TXE   ((blt_int16u)0x0080)           /* transmit data reg. empty bit  */
#define UART_BIT_RXNE  ((blt_int16u)0x0020)           /* read data reg. not empty bit  */


/****************************************************************************************
* Register definitions
****************************************************************************************/
#if (BOOT_COM_UART_CHANNEL_INDEX == 0)
/* set UART base address to USART1 */
#define UARTx          ((tUartRegs *) (blt_int32u)0x40013800)
#elif (BOOT_COM_UART_CHANNEL_INDEX == 1)
/* set UART base address to USART2 */
#define UARTx          ((tUartRegs *) (blt_int32u)0x40004400)
#else
/* set UART base address to USART1 by default */
#define UARTx          ((tUartRegs *) (blt_int32u)0x40013800)
#endif


/****************************************************************************************
* Function prototypes
****************************************************************************************/
static blt_bool UartReceiveByte(blt_int8u *data);
static blt_bool UartTransmitByte(blt_int8u data);


/****************************************************************************************
** NAME:           UartInit
** PARAMETER:      none
** RETURN VALUE:   none
** DESCRIPTION:    Initializes the UART communication interface
**
****************************************************************************************/
void UartInit(void)
{
  /* the current implementation supports USART1 and USART1. throw an assertion error in 
   * case a different UART channel is configured.  
   */
  ASSERT_CT((BOOT_COM_UART_CHANNEL_INDEX == 0) || (BOOT_COM_UART_CHANNEL_INDEX == 1)); 
  /* first reset the UART configuration. note that this already configures the UART
   * for 1 stopbit, 8 databits and no parity.
   */
  UARTx->BRR = 0;
  UARTx->CR1 = 0;
  UARTx->CR2 = 0;
  UARTx->CR3 = 0;
  UARTx->GTPR = 0;
  /* configure the baudrate, knowing that PCLKx is configured to be half of
   * BOOT_CPU_SYSTEM_SPEED_KHZ.
   */
  UARTx->BRR = ((BOOT_CPU_SYSTEM_SPEED_KHZ/2)*(blt_int32u)1000)/BOOT_COM_UART_BAUDRATE;
  /* enable the UART including the transmitter and the receiver */
  UARTx->CR1 |= (UART_BIT_UE | UART_BIT_TE | UART_BIT_RE);
} /*** end of UartInit ***/


/****************************************************************************************
** NAME:           UartTransmitPacket
** PARAMETER:      data pointer to byte array with data that it to be transmitted.
**                 len  number of bytes that are to be transmitted.
** RETURN VALUE:   none
** DESCRIPTION:    Transmits a packet formatted for the communication interface.
**
****************************************************************************************/
void UartTransmitPacket(blt_int8u *data, blt_int8u len)
{
  blt_int16u data_index;

  /* verify validity of the len-paramenter */
  ASSERT_RT(len <= BOOT_COM_TX_MAX_DATA);  

  /* first transmit the length of the packet */  
  ASSERT_RT(UartTransmitByte(len) == BLT_TRUE);  
  
  /* transmit all the packet bytes one-by-one */
  for (data_index = 0; data_index < len; data_index++)
  {
    /* keep the watchdog happy */
    CopService();
    /* write byte */
    ASSERT_RT(UartTransmitByte(data[data_index]) == BLT_TRUE);  
  }
} /*** end of UartTransmitPacket ***/


/****************************************************************************************
** NAME:           UartReceivePacket
** PARAMETER:      data pointer to byte array where the data is to be stored.
** RETURN VALUE:   BLT_TRUE if a packet was received, BLT_FALSE otherwise.
** DESCRIPTION:    Receives a communication interface packet if one is present.
**
****************************************************************************************/
blt_bool UartReceivePacket(blt_int8u *data)
{
  static blt_int8u xcpCtoReqPacket[XCP_CTO_PACKET_LEN+1];  /* one extra for length */
  static blt_int8u xcpCtoRxLength;
  static blt_bool  xcpCtoRxInProgress = BLT_FALSE;

  /* start of cto packet received? */
  if (xcpCtoRxInProgress == BLT_FALSE)
  {
    /* store the message length when received */
    if (UartReceiveByte(&xcpCtoReqPacket[0]) == BLT_TRUE)
    {
      /* indicate that a cto packet is being received */
      xcpCtoRxInProgress = BLT_TRUE;

      /* reset packet data count */
      xcpCtoRxLength = 0;
    }
  }
  else
  {
    /* store the next packet byte */
    if (UartReceiveByte(&xcpCtoReqPacket[xcpCtoRxLength+1]) == BLT_TRUE)
    {
      /* increment the packet data count */
      xcpCtoRxLength++;

      /* check to see if the entire packet was received */
      if (xcpCtoRxLength == xcpCtoReqPacket[0])
      {
        /* copy the packet data */
        CpuMemCopy((blt_int32u)data, (blt_int32u)&xcpCtoReqPacket[1], xcpCtoRxLength);        
        /* done with cto packet reception */
        xcpCtoRxInProgress = BLT_FALSE;

        /* packet reception complete */
        return BLT_TRUE;
      }
    }
  }
  /* packet reception not yet complete */
  return BLT_FALSE;
} /*** end of UartReceivePacket ***/


/****************************************************************************************
** NAME:           UartReceiveByte
** PARAMETER:      data pointer to byte where the data is to be stored.
** RETURN VALUE:   BLT_TRUE if a byte was received, BLT_FALSE otherwise.
** DESCRIPTION:    Receives a communication interface byte if one is present.
**
****************************************************************************************/
static blt_bool UartReceiveByte(blt_int8u *data)
{
  /* check if a new byte was received by means of the RDR-bit */
  if((UARTx->SR & UART_BIT_RXNE) != 0)
  {
    /* store the received byte */
    data[0] = UARTx->DR;
    /* inform caller of the newly received byte */
    return BLT_TRUE;
  }
  /* inform caller that no new data was received */
  return BLT_FALSE;
} /*** end of UartReceiveByte ***/


/****************************************************************************************
** NAME:           UartTransmitByte
** PARAMETER:      data value of byte that is to be transmitted.
** RETURN VALUE:   BLT_TRUE if the byte was transmitted, BLT_FALSE otherwise.
** DESCRIPTION:    Transmits a communication interface byte.
**
****************************************************************************************/
static blt_bool UartTransmitByte(blt_int8u data)
{
  /* check if tx holding register can accept new data */
  if ((UARTx->SR & UART_BIT_TXE) == 0)
  {
    /* UART not ready. should not happen */
    return BLT_FALSE;
  }
  /* write byte to transmit holding register */
  UARTx->DR = data;
  /* wait for tx holding register to be empty */
  while((UARTx->SR & UART_BIT_TXE) == 0) 
  { 
    /* keep the watchdog happy */
    CopService();
  }
  /* byte transmitted */
  return BLT_TRUE;
} /*** end of UartTransmitByte ***/
#endif /* BOOT_COM_UART_ENABLE > 0 */


/*********************************** end of uart.c *************************************/