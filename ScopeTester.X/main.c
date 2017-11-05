/**
 * Scope Tester
 * Author: https://github.com/cosenmarco
 * License: https://creativecommons.org/licenses/by-sa/4.0/legalcode
 * Processor: PIC16F1823
 * This is the C (Microchip XC8 compiler) source for the project discussed at 
 * http://www.eevblog.com/forum/projects/oscilloscope-tester-like-hameg-hz60/
*/

// CONFIG1
#pragma config FOSC = HS        // Oscillator Selection (HS Oscillator, High-speed crystal/resonator connected between OSC1 and OSC2 pins)
#pragma config WDTE = OFF       // Watchdog Timer Enable (WDT disabled)
#pragma config PWRTE = ON       // Power-up Timer Enable (PWRT enabled)
#pragma config MCLRE = OFF      // MCLR Pin Function Select (MCLR/VPP pin function is digital input)
#pragma config CP = OFF         // Flash Program Memory Code Protection (Program memory code protection is disabled)
#pragma config CPD = OFF        // Data Memory Code Protection (Data memory code protection is disabled)
#pragma config BOREN = ON       // Brown-out Reset Enable (Brown-out Reset enabled)
#pragma config CLKOUTEN = OFF   // Clock Out Enable (CLKOUT function is disabled. I/O or oscillator function on the CLKOUT pin)
#pragma config IESO = OFF       // Internal/External Switchover (Internal/External Switchover mode is disabled)
#pragma config FCMEN = OFF      // Fail-Safe Clock Monitor Enable (Fail-Safe Clock Monitor is disabled)

// CONFIG2
#pragma config WRT = OFF        // Flash Memory Self-Write Protection (Write protection off)
#pragma config PLLEN = ON       // PLL Enable (4x PLL enabled) -- Fosc will be 32MHz with our 8MHz crystal
#pragma config STVREN = ON      // Stack Overflow/Underflow Reset Enable (Stack Overflow or Underflow will cause a Reset)
#pragma config BORV = LO        // Brown-out Reset Voltage Selection (Brown-out Reset Voltage (Vbor), low trip point selected.)
#pragma config LVP = ON         // Low-Voltage Programming Enable (Low-voltage programming enabled)


#include <xc.h>
#include <stdint.h>

//#define _XTAL_FREQ 500000      //Used by the XC8 delay_ms(x) macro

#define LED_GRN PORTAbits.RA0
#define LED_RED PORTAbits.RA1
#define ERR PORTAbits.RA2
#define SWITCH PORTAbits.RA3
#define SHUTDOWN PORTCbits.RC0
#define SENSE PORTCbits.C12IN1N
#define FSEL0 PORTCbits.RC2
#define FSEL1 PORTCbits.RC3
#define FSEL2 PORTCbits.RC4
#define OUT PORTCbits.RC5

#define SW_PRESSED 0
#define SW_RELEASED 1

#define SHTDN_KEEP_ON 1
#define SHTDN_TURN_OFF 0

#define DC_OUT_LEVEL 0

/********** DEFINES **********/
typedef struct {
    uint8_t Pre;
    uint16_t Period;
    uint8_t Duty;
} freq_t;

typedef enum { 
    F_1MHz,
    F_100KHz,
    F_10KHz,
    F_1KHz,
    F_100Hz,
    F_10Hz,
    F_1Hz,
    F_DC
} mode_t;

void turnOnGreenLed(void);
void turnOnRedLed(void);
unsigned readMode(void);
void setupMode(mode_t mode);

void main(void) {
    
    INTCONbits.GIE = 1; // Enable global interrupt
    INTCONbits.PEIE = 1; // Enable peripheral interrupt
    
    // PORT A configuration
    PORTA = 0;
    TRISA = 0b111100; // Input from the TS2951 Error pin and the ON/OFF button
    WPUA = 0b001100; // Weak Pull-up enalbed on ERR and SWITCH inputs
    
    // PORT C configuration
    PORTC = 0;
    TRISC = 0b011110; // We use here RC0 for SHUTDOWN and RC5 as the signal OUT
    WPUC = 0b011100; // We want to pull-up all the frequency seletion switches

    OPTION_REGbits.nWPUEN = 0; // Enable Weak Pull-up resistors
    
    SHUTDOWN = SHTDN_KEEP_ON; // Make sure circuit stays ON
    
    // Given how the circuit boots, it is expected that upon startup the
    // SWITCH is pressed. Do nothing until the user releases it.
    while(SWITCH == SW_PRESSED) continue;

    mode_t mode = F_DC;
    mode_t oldmode = F_DC;
    
    setupMode(mode);
    turnOnGreenLed();

    do {        
        mode = readMode();
        if(mode != oldmode) {
            oldmode = mode;
            setupMode(mode);
        }
    } while(SWITCH == SW_RELEASED);
    
    // Do nothing while the user keeps the switch pressed
    while(SWITCH == SW_PRESSED) continue;
    
    // User released ON/OFF. Shutdown now.
    setupMode(F_DC);
    SHUTDOWN = SHTDN_TURN_OFF; // Turn off power supply
}

void turnOnGreenLed(void) {
    LED_GRN = 1;
    LED_RED = 0;
}

void turnOnRedLed(void) {
    LED_GRN = 0;
    LED_RED = 1;
}

unsigned readMode() {
    return (unsigned) ((FSEL2 << 2) + (FSEL1 << 1) + FSEL0);
}

void setupMode(mode_t mode) {
    switch(mode) {
        // Clocking Timer1 from the system clock (F OSC ) should not be used in Compare mode.
        case F_1MHz:
            // Every 4 counts we need to toggle the output
            CCPR1H = 0;
            CCPR1L = 4;
            break;
            
        case F_100KHz:
            // Every 40 counts we need to toggle the output
            CCPR1H = 0;
            CCPR1L = 40;
            break;
            
        case F_10KHz:
            // Every 400 (0x190) counts we need to toggle the output
            CCPR1H = 0x1;
            CCPR1L = 0x90;
            break;
            
        case F_1KHz:
            // Every 4000 (0xFA0) counts we need to toggle the output
            CCPR1H = 0xF;
            CCPR1L = 0xA0;
            break;
            
        case F_100Hz:
            // Every 40000 (0x9C40) counts we need to toggle the output
            CCPR1H = 0x9C;
            CCPR1L = 0x40;
            break;

        case F_10Hz:
        case F_1Hz:
            // Every 50000 (0xC350) counts we need to toggle the output or generate an interrupt
            CCPR1H = 0xC3;
            CCPR1L = 0x50;
            break;
    }
    
    switch(mode) {
        case F_1MHz:
        case F_100KHz:
        case F_10KHz:
        case F_1KHz:
        case F_100Hz:
            PIE1bits.CCP1IE = 0; // Disables the CCP1 interrupt
            T1CON = 0b01000101; // Fosc/4 (8MHz), 1:1 prescale, ON
            CCP1CONbits.CCP1M = 0b0010; // Compare mode: toggle output on match
            break;

        case F_10Hz:
            PIE1bits.CCP1IE = 0; // Disables the CCP1 interrupt
            T1CON = 0b01110101; // Fosc/4 (8MHz), 1:8 prescale, ON
            CCP1CONbits.CCP1M = 0b0010; // Compare mode: toggle output on match
            break;

        case F_1Hz:
            PIE1bits.CCP1IE = 1; // Enables the CCP1 interrupt
            PIR1bits.CCP1IF = 0; // Clear interrupt bit
            T1CON = 0b01110101; // Fosc/4 (8MHz), 1:8 prescale, ON
            CCP1CONbits.CCP1M = 0b1010; // Compare mode: generate software interrupt only
            break;
            
        case F_DC:
        default:
            PIE1bits.CCP1IE = 0; // Disables the CCP1 interrupt
            T1CONbits.T1OSCEN = 0; // Turn off Timer1
            CCP1CONbits.CCP1M = 0; // Capture/Compare/PWM off (resets ECCP1 module)
            OUT = DC_OUT_LEVEL; // Set OUT to the correct logic level to have a pure DC output
            break;
    }

}
               
volatile uint8_t ocState = 0;

void interrupt isr(void) {
    if (PIR1bits.CCP1IF) {
        PIR1bits.CCP1IF = 0; // Clear interrupt bit
        // Every 10 calls toogle the OUT pin
        if (++ocState >= 10) {
            ocState = 0;
            OUT = ~OUT;
        }
    }
}

