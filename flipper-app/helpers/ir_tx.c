#include "ir_tx.h"
#include <infrared_transmit.h>

// One NECext frame per call. The backpack's IRModule decodes (address, command) and acts on
// address 0x474D. The Flipper's NECext encoder is the same one that produced GhostMeshBackpack.ir,
// so FAP transmit and the .ir remote are byte-for-byte identical on the wire.
void ghostmesh_ir_send(uint8_t command) {
    InfraredMessage msg = {
        .protocol = InfraredProtocolNECext,
        .address = GHOSTMESH_IR_ADDR,
        .command = command,
        .repeat = false,
    };
    infrared_send(&msg, 1);
}
