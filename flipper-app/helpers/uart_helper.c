#include "uart_helper.h"

#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <string.h>
#include <stdlib.h>

struct UartHelper {
    FuriHalSerialHandle* handle;
    UartHelperRxCallback rx_cb;
    void* context;
    bool active;
};

static void uart_internal_rx_cb(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* ctx)
{
    UartHelper* helper = ctx;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(helper->rx_cb) {
            helper->rx_cb(byte, helper->context);
        }
    }
}

UartHelper* uart_helper_alloc(uint32_t baud, UartHelperRxCallback rx_cb, void* context) {
    UartHelper* helper = malloc(sizeof(UartHelper));
    if(!helper) return NULL;

    helper->rx_cb = rx_cb;
    helper->context = context;
    helper->active = false;

    helper->handle = furi_hal_serial_control_acquire(GHOSTMESH_UART_ID);
    if(!helper->handle) {
        free(helper);
        return NULL;
    }

    furi_hal_serial_init(helper->handle, baud);
    furi_hal_serial_async_rx_start(helper->handle, uart_internal_rx_cb, helper, false);
    helper->active = true;

    return helper;
}

void uart_helper_free(UartHelper* helper) {
    if(!helper) return;
    if(helper->active) {
        furi_hal_serial_async_rx_stop(helper->handle);
        furi_hal_serial_deinit(helper->handle);
        furi_hal_serial_control_release(helper->handle);
    }
    free(helper);
}

void uart_helper_send(UartHelper* helper, const char* str) {
    if(!helper || !helper->active || !str) return;
    size_t len = strlen(str);
    if(len == 0) return;
    furi_hal_serial_tx(helper->handle, (const uint8_t*)str, len);
    furi_hal_serial_tx_wait_complete(helper->handle);
}

void uart_helper_send_bytes(UartHelper* helper, const uint8_t* data, size_t len) {
    if(!helper || !helper->active || !data || len == 0) return;
    furi_hal_serial_tx(helper->handle, data, len);
    furi_hal_serial_tx_wait_complete(helper->handle);
}

bool uart_helper_is_active(const UartHelper* helper) {
    return helper != NULL && helper->active;
}
