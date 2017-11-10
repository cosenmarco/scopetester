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


// OUTPUTS
#define LED_GRN LATAbits.LATA0
#define LED_RED LATAbits.LATA1
#define SHUTDOWN LATCbits.LATC0
#define OUT LATCbits.LATC5

// INPUTS
#define ERR PORTAbits.RA2
#define SWITCH PORTAbits.RA3
#define SENSE PORTCbits.C12IN1N
#define FSEL0 PORTCbits.RC2
#define FSEL1 PORTCbits.RC3
#define FSEL2 PORTCbits.RC4

#define SW_PRESSED 0
#define SW_RELEASED 1

#define SHTDN_KEEP_ON 1
#define SHTDN_TURN_OFF 0

#define DC_OUT_LEVEL 0

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

typedef struct {
    unsigned prescale;
    unsigned pr2Value;
    unsigned ccpr1lValue;
    unsigned dc1bValue;
} pwm_setup_t;

typedef struct {
    unsigned prescale;
    unsigned ccpr1hValue;
    unsigned ccpr1lValue;
} compare_setup_t;

void turnOnGreenLed(void);
void turnOnRedLed(void);
unsigned readMode(void);
void setupMode(mode_t mode);
void setupPWM(pwm_setup_t setup);
void setupCompare(compare_setup_t setup);
void setupDC(void);

void main(void) {
    
    INTCONbits.GIE = 1; // Enable global interrupt
    INTCONbits.PEIE = 1; // Enable peripheral interrupt
    
    // PORT A configuration
    PORTA = 0;
    ANSELA=0;
    TRISA = 0b111100; // Input from the TS2951 Error pin and the ON/OFF button
    WPUA = 0b001100; // Weak Pull-up enalbed on ERR and SWITCH inputs
    
    // PORT C configuration
    PORTC = 0;
    ANSELC=0;
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



// Values obtained for a Fosc = 32MHz
const pwm_setup_t pwm_setup[] = {
    // Prescale,   PR2,      CCPR1L,  CCP1CON
    {      0b00,     7,       0b100,     0b00  }, // F_1MHz
    {      0b00,    79,    0b101000,     0b00  }, // F_100KHz
    {      0b01,   199,   0b1100100,     0b00  }, // F_10KHz
    {      0b11,   124,    0b111110,     0b10  }  // F_1KHz
};

// Values obtained for a Fosc = 32MHz
const compare_setup_t compare_setup[] = {
// Prescale,  CCPR1H,  CCPR1L
    {  0b00,    0x0F,    0xA0 }, // F_100Hz: Every 4000 (0xFA0) counts
    {  0b00,    0x9C,    0X40 }, // F_10Hz: Every 40000 (0x9C40) counts
    {  0b11,    0XC3,    0x50 }  // F_1Hz: Every 50000 (0xC350) counts
};

/**
 * Sets up the operating frequency which will be generated at pin OUT.
 * The modes F_1MHz to F_1KHz use the PWM module according to the config
 * table stored in pwm_setup.
 * The modes F_100Hz to F_1Hz use the Compare mode to generate an interrupt
 * at different frequencies. The ISR will always divide this frequency by
 * 10, so this must be taken into account in the setup of Timer1 and CCPR1.
 * Clocking Timer1 from the system clock (F OSC ) should not be used in Compare
 * mode. This means Fosc/4 is 8MHz
 * @param mode The chosen operating mode
 */
void setupMode(mode_t mode) {
    setupDC(); // Reset everything and provide default

    switch(mode) {
        case F_1MHz:
        case F_100KHz:
        case F_10KHz:
        case F_1KHz:
            setupPWM(pwm_setup[mode]);
            break;

        case F_100Hz:
        case F_10Hz:
        case F_1Hz:
            setupCompare(compare_setup[mode - F_100Hz]);
            break;
    }
}

void setupPWM(pwm_setup_t setup) {
    // Configure Timer2 and PWM
    T2CONbits.T2CKPS = setup.prescale;
    PR2 = setup.pr2Value;
    CCPR1L = setup.ccpr1lValue;
    CCP1CONbits.DC1B = setup.dc1bValue;
    TMR2 = 0xFF;

    // Start Timer2 and PWM
    CCP1CONbits.CCP1M = 0b1100; // PWM mode: P1A, P1C active-high; P1B, P1D active-high
    T2CONbits.TMR2ON = 1;
}

void setupCompare(compare_setup_t setup) {
    PIE1bits.CCP1IE = 1; // Enables the CCP1 interrupt
    CCP1CONbits.CCP1M = 0b1010; // Compare mode: generate software interrupt only
    T1CONbits.TMR1CS = 0b00; // Fosc/4 (8MHz)
    T1CONbits.T1CKPS = setup.prescale;
    CCPR1H = setup.ccpr1hValue;
    CCPR1L = setup.ccpr1lValue;
    T1CONbits.TMR1ON = 1; // Turn ON Timer1
}

void setupDC() {
    CCP1CONbits.CCP1M = 0; // Capture/Compare/PWM off (resets ECCP1 module)
    OUT = DC_OUT_LEVEL; // Set OUT to the correct logic level to have a pure DC output

    // Turn OFF and reset Timer1
    T1CONbits.TMR1ON = 0;
    TMR1L = 0;
    TMR1H = 0;

    T2CONbits.TMR2ON = 0; // Turn off Timer2
    PIE1bits.CCP1IE = 0; // Disables the CCP1 interrupt
}
               
volatile uint8_t ocState = 0;

void interrupt isr(void) {
    if (PIR1bits.CCP1IF) {
        PIR1bits.CCP1IF = 0; // Clear interrupt bit
        
        // Resets the Timer1
        TMR1H = 0;
        TMR1L = 0;

        // Every 10 calls toogle the OUT pin
        if (++ocState >= 10) {
            ocState = 0;
            OUT = ~OUT;
        }
    }
}

