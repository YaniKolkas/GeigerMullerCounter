/* ========================================================================== */
/*                                                                            */
/*   main.c                                                                   */
/*   (c) 2020 Yani Kolkas                                                     */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/*   Main entry point for GeigerMuller Project                                */
/*                                                                            */
/*                                                                            */
/*                                                                            */
/*                                                                            */
/* ========================================================================== */


#include <msp430.h> 
#include <stdint.h>

//DEFINES

//GM TUBE FACTOR CPM to uSv/h
#define TUBE_FACTOR  57                       // uSv/h = 0.0057 * CPM

//MCU PIN defines
#define GM_INPUT      BIT4                    //INPUT FROM GM TUBE CATHODE
#define RED_LED       BIT0                    //RED LED installed on PCB - used for debug
#define GREEN_LED     BIT6                    //GREEN LED installed on PCB - used for debug
#define SWITCH        BIT3                    //BUTTON - USED FOR DEBUG
#define UART_TX_PIN   BIT1                    //SW UART TX LINE

#define ENDLESS_LOOP()   while(1){}

//TIMER DEFINES FOR generating timer interval on timer A on 32768/64 timebase OBSOLETE
#define SEC_DELAY      512
#define TEN_SEC_DELAY  (10U * SEC_DELAY)
#define MINUTE_DELAY   (60U * SEC_DELAY)
#define MINUTE          60U

//Period of 1Bit for 9600 Baud SW UART, SMCLK = 1MHz
#define UART_TBIT           (1000000 / 9600)

#define ASCII_DIGIT_START    48
#define MAX_NUMBER_DIGITS    5     // 5 digits for uint16 - max is 65535

//GLOBALS
uint16_t txData;    // UART internal variable for 1 byte payload. 1 byte payload = 10 bits total

//Count per minutes
static uint16_t cpmCounter=0;
static uint16_t cpmCurrentCounter=0;

//Counter per  second
static uint8_t  secCounter =0;


void initPorts(void)
{

    //32768 XTAL INIT
    P2DIR = 0;                                      //SET ALL P2 INPUTS
    P2REN = BIT5 | BIT4 | BIT3 | BIT2 | BIT1;       //ENABLE RES on P2 PINS 0 to 5
    P2OUT = 0;                                      //Enable pull down on P2
    P2SEL = BIT7 | BIT6;                            //SET PIN2.6 and PIN2.7 for 32Khz crystal

    //LED AND SWITCH INIT
    P1DIR |= RED_LED | GREEN_LED |UART_TX_PIN;         //SET LED in output
    P1OUT &= ~RED_LED & ~GREEN_LED;                    //SET OUTPUT to 0
    P1DIR &= ~SWITCH;                                  //SET input from button
    P1DIR &= ~GM_INPUT;                                //SET input from GM_TUBE
    P1REN |= SWITCH | GM_INPUT;                        //enable pull-up/pull-down on inputs
    P1OUT |= SWITCH | GM_INPUT;                        //set usage of pull up

    //SET UART LINE TO IDLE in 1
    P1OUT |= UART_TX_PIN;

    P1IES |= SWITCH | GM_INPUT;      //set interrupt on
    P1IFG = 0;                       //clear all pending interrupt flags
    P1IE  |= SWITCH | GM_INPUT;      // enable interrupt on

}

//Initialization of ACLK from 32758 XTAL
void initACLK(void)
{
    BCSCTL1 |= DIVA_0;      // ACLK=LFXT1 = 32768Hz
    BCSCTL3 |= XCAP_3;      //12.5pF cap- setting for 32768Hz crystal for LFXT1
}


//Initialization of Digital Controlled oscillator on 1MHz,
// needed for SW UART
void initDCO(void)
{
    if (0xFF == CALBC1_1MHZ)              // Check if calibration constant erased
    {
        ENDLESS_LOOP();                      // trap in endless loop
    }

    DCOCTL = 0;                         // Select lowest DCO settings
    BCSCTL1 = CALBC1_1MHZ;              // Set DCO to 1 MHz
    DCOCTL = CALDCO_1MHZ;
}


// Function configures Timer_A for UART operation
void TimerA_UART_init(void)
{
    TACCTL0 &= ~CCIE;                       //Disable interrupts
    TACTL = TASSEL_2 | MC_1;                // SMCLK, start in up mode
}

// Used Watchdog in Timer+ mode - interrupt on 1 sec
void initWatchDogTimerPlus(void)
{
    WDTCTL = WDT_ADLY_1000;  //set for 1 sec on 32768 clock
    IE1 |= WDTIE;
}

// Send one byte using the Timer_A SW UART
void TimerA_UART_tx(unsigned char byte)
{
    while (TACCTL0 & CCIE);          // Ensure last char is transmitted
    TACCR0 = UART_TBIT;              // Time duration of 1 bit send
    txData = byte;                   // Load global variable handled by interrupt routine
    txData |= 0x100;                 // Add "1" stop bit to TXData
    txData <<= 1;                    // Add "0" start bit

    TAR=0;                          //start counting from 1 full bit time
                                    // give enough time the line to be in idle before transmitted

    TACCTL0 |=  CCIE;               // Enable TimerA interrupts

    while ( CCTL0 & CCIE );         // loop until everything is transmitted
}

// Send a string through UART
void TimerA_UART_print(char *string)
{
    while (*string)
    {
        TimerA_UART_tx(*string++);
    }
}

void intToChars(const uint16_t *input, uint8_t *charArray  )
{
    uint16_t temp = *input;
    uint8_t i=0;

    for ( i=0; i<MAX_NUMBER_DIGITS; i++ )
    {
        charArray[MAX_NUMBER_DIGITS-1-i] = (uint8_t)(temp % 10u ) + ASCII_DIGIT_START;
        temp = temp / 10;
    }

}

void main(void)
{
    WDTCTL = WDTPW | WDTHOLD;       // stop watchdog timer

    initDCO();
    initPorts();
    initACLK();
    TimerA_UART_init();
    initWatchDogTimerPlus();

    P1OUT |= GREEN_LED;             // Give green light to the world

    __bis_SR_register(GIE);         //enable global interrupt

    uint16_t uSvPerHour=0;
    uint8_t  uSvPerHourAscii[MAX_NUMBER_DIGITS+1] = {0, 0, 0, 0, 0, '\0'};
    uint8_t  cpmAscii[MAX_NUMBER_DIGITS+1] = {0, 0, 0, 0, 0, '\0'};
    uint8_t  counterAscii[MAX_NUMBER_DIGITS+1] = {0, 0, 0, 0, 0, '\0'};
    uint8_t  secondsAscii[MAX_NUMBER_DIGITS+1] = {0, 0, 0, 0, 0, '\0'};

    uint8_t has_send_data = 0;

    while(1)
    {

        if(secCounter % 2U != 0)
        {
            has_send_data = 0;     // Prepare single sending
        }


        if(  (secCounter % 2U == 0) && has_send_data ==0 )
        {

            uSvPerHour = cpmCounter * TUBE_FACTOR;

            intToChars(&cpmCounter, cpmAscii);
            intToChars(&cpmCurrentCounter, counterAscii);
            intToChars(&uSvPerHour, uSvPerHourAscii);
            uint16_t secIn=secCounter;
            intToChars(&secIn, secondsAscii);


            //TEST UART
            TimerA_UART_print("RELATIVE TIME = ");
            TimerA_UART_print(secondsAscii);
            TimerA_UART_print(" seconds, GEIGER MULLER PROTOTYTPE REPORTING CURRENT COUNTER = ");
            TimerA_UART_print( counterAscii  );
            TimerA_UART_print(", CPM = ");
            TimerA_UART_print( cpmAscii  );
            TimerA_UART_print(", uSv/h = ");
            TimerA_UART_print(uSvPerHourAscii);
            TimerA_UART_print("\n\r");

            has_send_data = 1;
        }

    }
}


// Interrupt routine handling aticle counting and button pressed
#pragma vector=PORT1_VECTOR
__interrupt void ISR_portChange(void)
{
    // Particle detected
    if(P1IFG & GM_INPUT)
    {
        cpmCurrentCounter++;
        P1OUT ^= RED_LED;   //toggle RED
        P1IFG &= ~GM_INPUT;
    }

    //button pressed
    if(P1IFG & SWITCH)
    {
       P1OUT ^= GREEN_LED;   //toggle GREEN led
       P1IFG &= ~SWITCH; // clear interrupt flag
    }

}


//Interrupt from TimerA used for SW UART implementation
#pragma vector=TIMERA0_VECTOR
__interrupt void ISR_Timer_A_expire (void)
{
    static unsigned char txBitCnt = 10;

     if (txBitCnt == 0)
     {                                       // All bits TXed?
         TACCTL0 &= ~CCIE;                   // All bits TXed, disable interrupt
         txBitCnt = 10;                      // Re-load bit counter
     }
     else
     {
         if (txData & 0x01)
         {
             P1OUT |= UART_TX_PIN;    //Transmit 1 as high level
         }
         else
         {
             P1OUT &= ~UART_TX_PIN;  //Transmit 0 as low level
         }

         txData >>= 1; //move to the next bit for sending
         txBitCnt--;  // mark another transferred bit
     }
}


#pragma vector=WDT_VECTOR
__interrupt void ISR_WDT_expire (void)
{
    // Test code just toggle the LEDs
    //P1OUT ^= RED_LED | GREEN_LED;   //toggle led , no need to clean CCIFG it is automatically reset

    secCounter++;              //increment perSecond counter

    if( MINUTE == secCounter )
    {
        cpmCounter = cpmCurrentCounter;
        cpmCurrentCounter = 0;
        secCounter = 0;
    }

}











