#pragma once

#include <furi.h>
#include <furi_hal.h>

// USART1 on Flipper GPIO pins 13 (TX) and 14 (RX)
#define GHOSTMESH_UART_ID    FuriHalSerialIdUsart
#define GHOSTMESH_UART_BAUD  115200

typedef struct UartHelper UartHelper;

// Called from UART receive context for each incoming byte.
// Keep this callback short — do not block or call furi_delay_ms inside it.
typedef void (*UartHelperRxCallback)(uint8_t byte, void* context);

// Allocate and open UART. Returns NULL if the serial port cannot be acquired
// (e.g. another app already holds it).
UartHelper* uart_helper_alloc(uint32_t baud, UartHelperRxCallback rx_cb, void* context);

// Stop UART and free all resources.
void uart_helper_free(UartHelper* helper);

// Send a null-terminated string.
void uart_helper_send(UartHelper* helper, const char* str);

// Send raw bytes.
void uart_helper_send_bytes(UartHelper* helper, const uint8_t* data, size_t len);

// Returns true if UART was successfully opened.
bool uart_helper_is_active(const UartHelper* helper);
