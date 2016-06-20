/* Mains Frequency Monitor Project
 * Version 0.5
 * 
 * By: Dan Watson
 * syncchannel.blogspot.com
 * 5-16-2016
 * 
 * Device: PIC 16F1619
 * 
 * Note: Be sure to set double precision to 32-bit in XC8 linker properties
 * 
 * This program is shared under the Creative Commons Attribution-Share Alike 3.0 license
 * 
 */


#define _XTAL_FREQ 20000000.0 // Main oscillator frequency
#define MAINS_FREQ 60         // Set your mains frequency

#include <xc.h>
#include <stdio.h>
#include <stdbool.h>

// CONFIG1
#pragma config FOSC = ECH       // External Clock High Speed
#pragma config PWRTE = ON       // Power-up Timer enabled
#pragma config MCLRE = ON       // MCLR/VPP pin function is MCLR
#pragma config CP = OFF         // Program memory code protection is disabled
#pragma config BOREN = ON       // Brown-out Reset enabled
#pragma config CLKOUTEN = OFF   // CLKOUT function is disabled
#pragma config IESO = ON        // Internal External Switch Over mode is enabled
#pragma config FCMEN = ON       // Fail-Safe Clock Monitor is enabled

// CONFIG2
#pragma config WRT = OFF        // Write protection off
#pragma config PPS1WAY = ON     // The PPSLOCK bit cannot be cleared once it is set by software
#pragma config ZCD = ON         // ZCD enabled
#pragma config PLLEN = OFF      // PLL disabled
#pragma config STVREN = ON      // Stack Overflow or Underflow will cause a Reset
#pragma config BORV = LO        // Brown-out Reset Voltage (Vbor), low trip point selected
#pragma config LPBOR = OFF      // Pow-Power BOR is disabled
#pragma config LVP = ON         // Low-voltage programming enabled

// CONFIG3
#pragma config WDTCPS = WDTCPS1F// WDT Period Select (Software Control (WDTPS))
#pragma config WDTE = OFF       // Watchdog Timer disabled
#pragma config WDTCWS = WDTCWSSW// WDT Window Select (Software WDT window size control (WDTWS bits))
#pragma config WDTCCS = SWC     // WDT Input Clock Selector (Software control, controlled by WDTCS bits)

// Variables for data acquisition
volatile int acqCounter = 0;
volatile bool acqGate = false;
volatile double acqHolder = 0;
volatile double acqOut = 0;
volatile bool missedCycle = false;
volatile int erroneousZeroCrossing = 0;
volatile int acqErrZeroCross = 0;

void main(void)
{
    //I/O Setup
    ANSELA = 0x00; // Disable all analog inputs
    ANSELB = 0x00;
    ANSELC = 0x00;
    TRISB6 = 0; // PB6 Output for LED
    TRISB4 = 0; // PB4 Output for LED
    TRISC7 = 0; // RC7 Output for Tx Switch
    RB7PPS = 0b00010010; // Send USART TX to PB7
    
    
    //EUSART setup
    SYNC = 0; // Async
    SPEN = 1; // Enable
    SP1BRGL = 15; // 19200 baud @ 20MHz Clock
    TXEN = 1; // Enable transmitter
    
    //Initialize LCD (Parallax 2x16 Backlit #27977)
    __delay_ms(2000);
    putch(22); // Display on, cursor off, no blink
    __delay_ms(10);
    putch(17); // Backlight on
    __delay_ms(10);
    putch(12); // Move cursor to 0,0
    __delay_ms(10);
    
    //ZCD setup
    ZCD1EN = 1; // Enable ZCD
    ZCD1INTN = 1; // Interrupt on negative-going zero cross
    ZCDIE = 1; // Enable ZCD interrupts
    
    //SMT Setup
    SMT1CON1bits.MODE = 0b100; // Windowed measure mode
    SMT1CON1bits.REPEAT = 1; // Continuous acquisition
    SMT1CON0bits.EN = 1; // Enable SMT1
    SMT1WINbits.SMT1WSEL = 0b00101; // ZCD1 window select
    SMT1PRAIE = 1; // Enable interrupt on CPR update
    SMT1CON1bits.SMT1GO = 1; // Start SMT1 counting
    
    //AT Setup
    AT1CON0bits.AT1EN = 1; // Enable Angular Timer
    AT1SIGbits.AT1SSEL = 0b011; // Set ZCD as input signal
    AT1RES = 359; // Set resolution to 1 degree
    
    //Interrupt setup
    PEIE = 1; // Peripheral interrupts enable
    GIE = 1; // Global interrupt enable
    
    // Time Holders
    char hours = 21;
    char minutes = 48;
    char seconds = 00;
    
    LATC7 = 0; // Set Tx Switch to LCD side (1)
    
    // Lamp Test
    LATB4 = 1;
    LATB6 = 1;
    __delay_ms(750);
    LATB4 = 0;
    LATB6 = 0;
    
    unsigned long int uptimeCounter = 0ul;
    
    while(1)
    {
        if(acqGate)
        {
            acqGate = false;
            LATB6 = 1; // Turn on second indicator LED
            
            uptimeCounter++; // Increment uptime counter
            
            seconds++; // Increment time
            
            if (seconds > 59)
            {
                seconds = 0;
                minutes++;
                
                if (minutes > 59)
                {
                    minutes = 0;
                    hours++;
                    
                    if (hours > 23)
                    {
                        hours = 00;
                    }
                }
            } // Done incrementing time
            
            double freqHolder = acqOut/(double)(MAINS_FREQ - (acqErrZeroCross/2));
            
            // Update LCD display
            putch(12); // Move cursor to 0,0 to prepare for next send
            __delay_ms(2);
            printf("  FREQ: %5.3f      %02d:%02d:%02d    ", freqHolder, hours, minutes, seconds);
            
            // Output to serial terminal
            LATC7 = 0;
            __delay_ms(1);
            printf("%5.3f,%d:%d:%d,%d\n\r",freqHolder, hours, minutes, seconds, uptimeCounter);
            __delay_ms(1);
            LATC7 = 1;
            
            // Clean up
            acqOut = 0;
            acqErrZeroCross = 0;
            LATB6 = 0; // Turn off second indicator LED
            
            if (missedCycle) // Update missed cycle indicator LED
            {
                LATB4 = 0;
                missedCycle = false;
            } else {
                LATB4 = 1;
            }
        }
    }
    
    return;
}

// This ISR handles interrupt flags from SMT and ZCD
void interrupt ISR()
{
    if (SMT1PRAIF) // Finished measuring a cycle
    {
        SMT1PRAIF = 0;
        
        double measurementHolder = _XTAL_FREQ / (double)SMT1CPR;
        
        if (measurementHolder < 130 && measurementHolder > 110)
        {
            erroneousZeroCrossing++;
            
            if (erroneousZeroCrossing % 2 == 0 && erroneousZeroCrossing > 0)
            {
                acqCounter++;
            }
        }
        
        else
        {
            acqCounter++;
            acqHolder += measurementHolder;
        }
        
        if (acqCounter >= MAINS_FREQ) // Trigger update in main loop
        {
            acqCounter = 0;
            acqOut = acqHolder;
            acqGate = true;
            acqHolder = 0;
            acqErrZeroCross = erroneousZeroCrossing;
            erroneousZeroCrossing = 0;
        }
    }
    
    else if (ZCDIF) // Fires on negative-going zero crossing
    {
        ZCDIF = 0;
        
        if (AT1PHS < 177 || AT1PHS > 181) // Change values to adjust phase tolerance from 180 degrees
        {
            missedCycle = true;
        }
    }
}

// Write a character to the USART
void putch(unsigned char byte)
{
    TXREG = byte;
    while(!TXIF) continue;
    TXIF=0;
}