
// ========== Tuning / Configuration ==========
#define TIME_SEND_MS        50
#define SAMPLES_PER_CYCLE   25
#define LERP_ALPHA           0.80f
#define DEADZONE             12
#define BRAKE_OVERRIDE_LIM 100
#define RAMP_ENABLED         false
#define MAX_STEP            120

#define THROTTLE_PIN         35
#define BRAKE_PIN            34
#define THROTTLE_RAW_MIN     885
#define THROTTLE_RAW_MAX     3000
#define BRAKE_RAW_MIN        897
#define BRAKE_RAW_MAX        3000

#define UART0_RX_PIN  4
#define UART0_TX_PIN  5
#define UART1_RX_PIN 16
#define UART1_TX_PIN 17
#define UART2_RX_PIN 25
#define UART2_TX_PIN 26

#define HOVER_SERIAL_BAUD 115200
#define START_FRAME       0xABCD
#define STATUS_LINE_LEN   64

#define TELNET_TIMEOUT_MS 30000