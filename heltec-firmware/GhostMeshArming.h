#pragma once

// Shared arming state for the GhostMesh tamper modules.
//
// Set by ArmingModule (the slide switch on GPIO4) and read by the tamper modules
// (TiltModule, LightTamperModule, ProximityModule) — they only broadcast their alerts
// when the backpack is ARMED, so it can be handled/set up while DISARMED without spamming
// the mesh. Defined in ArmingModule.cpp.
extern volatile bool ghostmesh_armed;
