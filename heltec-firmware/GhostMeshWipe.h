#pragma once

// GhostMesh complete wipe — the "nuke".
//
// Erases EVERYTHING on the ESP32-S3 flash: NVS (keys, BLE bonds), every data partition (the
// LittleFS config + channel PSKs), and the firmware itself, then leaves the chip in ROM download
// mode. Recoverable only by reflashing firmware.factory.bin over USB and restoring config.
//
// This is a point of no return and it self-erases the running firmware — TEST IT ON A SPARE BOARD
// before trusting it on a deployment node. Called by CommandModule (mesh /wipe) and IRModule
// (IR ARM→WIPE→CONFIRM), both armed-gated + confirmed. Does not return.
void ghostmesh_complete_wipe();

// Set by any confirmed wipe path (mesh, physical button, IR) instead of erasing immediately, so
// CommandModule can play the wipe indicator effect first, then run the erase. Defined in
// CommandModule.cpp, serviced by its runOnce.
extern volatile bool ghostmesh_wipe_request;
