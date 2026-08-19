#pragma once
#include <stdint.h>

// GhostMesh IR command set — NECext, 16-bit address 0x474D ('GM'). These MUST match the backpack's
// decoder in heltec-firmware/IRModule.cpp and the buttons in GhostMeshBackpack.ir.
#define GHOSTMESH_IR_ADDR    0x474Du
#define GHOSTMESH_IR_ARM     0x01u
#define GHOSTMESH_IR_DISARM  0x02u
#define GHOSTMESH_IR_WIPE    0x03u
#define GHOSTMESH_IR_CONFIRM 0x04u

// Transmit one GhostMesh IR command over the Flipper's IR LED. Blocking (~tens of ms).
void ghostmesh_ir_send(uint8_t command);
