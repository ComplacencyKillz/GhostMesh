#include "textmsg_mode.h"

void textmsg_send(UartHelper* helper, const char* message) {
    if(!helper || !message) return;
    uart_helper_send(helper, message);
    uart_helper_send(helper, "\n");
}
