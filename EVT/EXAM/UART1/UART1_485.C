/********************************** (C) COPYRIGHT *********** ********************
* File Name: UART1_485.C
* Author: WCH
* Version: V1.3
* Date: 2016/06/24
* Description: Provides the configuration function of UART1, provides query mode for data transmission and reception, and the use of serial port FIFO.
                       To send and receive data in 485 mode of UART1, half duplex must be set
During the demonstration, other serial ports are required to input data to the serial port of CH559;
************************************************** *****************************/
#include "..\DEBUG.C" //Print debugging information
#include "..\DEBUG.H"

#pragma NOAREGS

#define CH559UART1_BPS 57600 /*Define CH559 serial port 1 communication baud rate*/
#define CH559UART1_FIFO_EN 1 //Enable CH559 serial port 1 receiving FIFO (receive and send 8 bytes each)

#if CH559UART1_FIFO_EN
UINT8 CH559UART1_FIFO_CNT;
#define CH559UART1_FIFO_TRIG 7 // FIFO full 7 bytes trigger interrupt (1, 2, 4, 7 optional)
#endif

UINT8 Str[] = {"hello world1234!"};

/************************************************* ******************************
* Function Name: UART1RegCfgValue()
* Description: CH559UART1 readable register value after correct configuration
* Input: None
* Output: None
* Return: None
************************************************** *****************************/
void UART1RegCfgValue()
{
    printf("SER1_IER %02X\n",(UINT16)SER1_IER); //0x27/0x17/0x37 are all possible
    printf("SER1_IIR %02X\n",(UINT16)SER1_IIR); //0xc1/0xC2 no interrupt or empty interrupt, "C" means FIFO is on
    printf("SER1_LCR %02X\n",(UINT16)SER1_LCR); //0x03 data format configuration, indicating wireless path interval, no parity, 1 stop bit, 8 data bits
    printf("SER1_MCR %02X\n",(UINT16)SER1_MCR); //0x08 interrupt output enable, excluding other functions such as flow control on
    printf("SER1_LSR %02X\n",(UINT16)SER1_LSR); //0x60, FIFO and line status
    printf("SER1_MSR %02X\n",(UINT16)SER1_MSR);
}

/************************************************* ******************************
* Function Name: ResetUART1()
* Description: CH559UART1 port soft reset
* Input: None
* Output: None
* Return: None
************************************************** *****************************/
void ResetUART1()
{
    SER1_IER |= bIER_RESET; //This bit can be automatically cleared to reset the serial port register
}

/************************************************* ******************************
* Function Name: CH559UART1Init(UINT8 DIV,UINT8 mode,UINT8 pin)
* Description: CH559 UART1 initialization setting
* Input:
                   UINT8 DIV sets the frequency division coefficient, clock frequency=Fsys/DIV, DIV cannot be 0
                   UINT8 mode, mode selection, 1: normal serial port mode; 0: 485 mode
                   UINT8 pin, serial port pin selection;
                   When mode=1
                   0: RXD1=P4.0, TXD1 is off;
                   1: RXD1&TXD1=P4.0&P4.4;
                   2: RXD1&TXD1=P2.6&P2.7;
                   3: RXD1&TXD1&TNOW=P2.6&P2.7&P2.5;
                   When mode=0
                   0: meaningless
                   1: Connect P5.4&P5.5 to 485, TNOW=P4.4;
                   2: P5.4&P5.5 connect to 485;
                   3: P5.4&P5.5 connect to 485, TNOW=P2.5;
* Output: None
* Return: None
************************************************** *****************************/
void CH559UART1Init(UINT8 DIV,UINT8 mode,UINT8 pin)
{
    UINT32 x;
    UINT8 x2;

    SER1_LCR |= bLCR_DLAB; // DLAB bit is 1, write DLL, DLM and DIV registers
    SER1_DIV = DIV; // Prescaler
    x = 10 * FREQ_SYS *2 / DIV / 16 / CH559UART1_BPS;
    x2 = x% 10;
    x /= 10;
    if (x2 >= 5) x ++; //Rounding
    SER1_DLM = x>>8;
    SER1_DLL = x&0xff;
    SER1_LCR &= ~bLCR_DLAB; //DLAB bit is 0 to prevent modification of UART1 baud rate and clock
    if(mode == 1) //Close RS485 mode RS485_EN = 0, can not be omitted
    {
XBUS_AUX |= bALE_CLK_EN;
    }
    else if(mode == 0) //Enable RS485 mode RS485_EN = 1;
    {
UHUB1_CTRL |= bUH1_DISABLE;
PIN_FUNC &= ~bXBUS_CS_OE;
PIN_FUNC |= bXBUS_AL_OE;
XBUS_AUX &= ~bALE_CLK_EN;
SER1_MCR |= bMCR_HALF; //485 mode can only use half-duplex mode
    }
    SER1_LCR |= MASK_U1_WORD_SZ; //Line control
    SER1_LCR &= ~(bLCR_PAR_EN | bLCR_STOP_BIT); //Wireless path interval, no parity, 1 stop bit, 8 data bits

    SER1_MCR &= ~bMCR_TNOW;
    SER1_IER |= bIER_EN_MODEM_O;
    SER1_IER |= ((pin << 4) & MASK_U1_PIN_MOD); //Serial port mode configuration
    SER1_IER |= /*bIER_MODEM_CHG | */bIER_LINE_STAT | bIER_THR_EMPTY | bIER_RECV_RDY;//Interrupt enable configuration

#if CH559UART1_FIFO_EN
    SER1_FCR |= MASK_U1_FIFO_TRIG | bFCR_T_FIFO_CLR | bFCR_R_FIFO_CLR | bFCR_FIFO_EN;//FIFO controller
                                                                               //Empty the receive and transmit FIFO, 7-byte receive trigger, FIFO enable
#endif
    SER1_MCR |= bMCR_OUT2; //MODEM control register
                                                                               //Interrupt request output, no actual interrupt
    SER1_ADDR |= 0xff; //Close multi-machine communication
}

/************************************************* ******************************
* Function Name: CH559UART1RcvByte()
* Description: CH559UART1 receives one byte
* Input: None
* Output: None
* Return: correct: UINT8 Rcvdat; receive data
************************************************** *****************************/
UINT8 CH559UART1RcvByte()
{
    while((SER1_LSR & bLSR_DATA_RDY) == 0); //Wait for the data to be ready
    return SER1_RBR;
}


#if CH559UART1_FIFO_EN
/************************************************* ******************************
* Function Name: CH559UART1Rcv(PUINT8 buf,UINT8 len)
* Description: CH559UART1 receives multiple bytes, FIFO must be opened
* Input: PUINT8 buf data storage buffer
                   UINT8 len Data pre-receive length
* Output: None
* Return: None
************************************************** *****************************/
void CH559UART1Rcv(PUINT8 buf,UINT8 len)
{
    UINT8 i,j;
    j = 0;
    while(len)
    {
         if(len >= CH559UART1_FIFO_TRIG)//The pre-receive length exceeds the FIFO receive trigger level
         {
             while((SER1_IIR & U1_INT_RECV_RDY) == 0);//Waiting for data available interrupt
             for(i=0;i<CH559UART1_FIFO_TRIG;i++)
             {
                 *(buf+j) = SER1_RBR;
                 j++;
             }
             len -= CH559UART1_FIFO_TRIG;
         }
         else
         {
             while((SER1_LSR & bLSR_DATA_RDY) == 0);//Wait for the data to be ready
             *(buf+j) = SER1_RBR;
             j++;
             len--;
         }
    }

}
#endif

/************************************************* ******************************
* Function Name: CH559UART1SendByte(UINT8 SendDat)
* Description: CH559UART1 sends one byte
* Input: UINT8 SendDat; the data to be sent
* Output: None
* Return: None
************************************************** *****************************/
void CH559UART1SendByte(UINT8 SendDat)
{
#if CH559UART1_FIFO_EN
    while(1)
    {
        if(SER1_LSR & bLSR_T_FIFO_EMP) CH559UART1_FIFO_CNT=8;//FIFO empty can fill up to 8 bytes
        if (CH559UART1_FIFO_CNT!=0)
        {
            SER1_THR = SendDat;
            CH559UART1_FIFO_CNT--;//FIFO count
            break;
        }
        while ((SER1_LSR & bLSR_T_FIFO_EMP) == 0 );//It is found that the FIFO is full and can only wait for the first 1 byte to be sent
}
#else
    while ((SER1_LSR & bLSR_T_ALL_EMP) == 0 );//Do not open FIFO, wait for the completion of 1 byte transmission
    SER1_THR = SendDat;
#endif
}

/************************************************* ******************************
* Function Name: CH559UART1SendStr(PUINT8 SendStr)
* Description: CH559UART1 sends multiple bytes
* Input: UINT8 SendStr; the first address of the data to be sent
* Output: None
* Return: None
************************************************** *****************************/
void CH559UART1SendStr(PUINT8 SendStr)
{
#if CH559UART1_FIFO_EN
    while(1)
    {
        if(SER1_LSR & bLSR_T_FIFO_EMP) CH559UART1_FIFO_CNT=8;//FIFO empty can fill up to 8 bytes
        while( CH559UART1_FIFO_CNT!=0)
        {
            if( *SendStr =='\0') break;//Send is complete
            SER1_THR = *SendStr++;
            CH559UART1_FIFO_CNT--;//FIFO count
        }
        if( *SendStr =='\0') break;//Send is complete
        while ((SER1_LSR & bLSR_T_FIFO_EMP) == 0 );//It is found that the FIFO is full and can only wait for the first 1 byte to be sent
}
#else
    while( *SendStr !='\0')
    {
        SER1_THR = *SendStr++;
        while ((SER1_LSR & bLSR_T_ALL_EMP) == 0 );//Do not open FIFO, wait for the completion of 1 byte transmission
    }
#endif
}

main()
{
   UINT8 i,j;
   UINT16 cnt;
   UINT8 buffer[20];
   UINT8 tmp[16]={0x13,0xab,0x5f,0x9d,0x32,0xde,0x56,0xaa,0x23,0x28,0x36,0x48,0x59,0x46,0x96,0xdd};
// CfgFsys( ); //Clock configuration
// mDelaymS(5); //Wait for the external crystal oscillator to stabilize

    mInitSTDIO( ); //Serial port 0, can be used for debugging
    printf("start ...\n");
To
    CH559UART1Init(13,0,2);
// UART1RegCfgValue( ); //UART1 register configuration
    P4_DIR |= 0xff; //When using P4 port, be sure to set the direction, TXD1 is set as output
    P4_OUT = 0xff;
    cnt = 0;
/*inquiry mode*/
   while(1)
   {
#if 0
       CH559UART1SendByte(0x13);//Data sending
       CH559UART1SendByte(0xab);
       CH559UART1SendByte(0x5f);
       CH559UART1SendByte(0x9d);
       CH559UART1SendByte(0x32);
       CH559UART1SendByte(0xde);
       CH559UART1SendByte(0x56);
       CH559UART1SendByte(0xAA);
       CH559UART1SendByte(0x23);
       CH559UART1SendByte(0x28);
       CH559UART1SendByte(0x36);
       CH559UART1SendByte(0x48);
       CH559UART1SendByte(0x59);
       CH559UART1SendByte(0x46);
       CH559UART1SendByte(0x96);
       CH559UART1SendByte(0xDD);
// CH559UART1SendStr(Str);
       mDelaymS(500);
       P4_OUT = ~P4_OUT;
#endif

#if 1
#if CH559UART1_FIFO_EN//Data receive
       CH559UART1Rcv(buffer,16);
       for(j=0;j<16;j++)
       {
           if(tmp[j] != buffer[j])
           {
              cnt++;
              printf("Error CNT: %d ",cnt);
              for(i = 0;i <16;i++)
              {
                  printf("%02X ",(UINT16)buffer[i]);
              }
              printf("\n");
PWM1_ = 0;//Data error
              break;
           }
           else
           {
              RXD1_ = ~RXD1_;//P4.0 flashes normal operation
           }
       }
       for(i = 0;i <16;i++)
       {
           printf("%02X ",(UINT16)buffer[i]);
       }
       printf("\n");
#else
       for(i=0;i<16;i++)
{
           buffer[i] = CH559UART1RcvByte();
       }
       for(i = 0;i <16;i++)
       {
           printf("%02X ",(UINT16)buffer[i]);
       }
       printf("\n");
#endif
#endif
   }
}
