#pragma once

#include "uart_helper.h"

// Send a text message in Meshtastic TEXTMSG serial mode.
//
// In TEXTMSG mode the node accepts a plain UTF-8 string terminated with '\n'
// and broadcasts it as a Meshtastic text message on the mesh. This function
// appends the required newline — do not include one in `message`.
//
// For future PROTO mode support, replace this function with proto_mode_send()
// in proto_mode.c once nanopb framing is implemented.
void textmsg_send(UartHelper* helper, const char* message);
