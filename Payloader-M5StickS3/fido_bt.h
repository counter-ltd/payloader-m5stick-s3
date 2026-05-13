#pragma once

void fidoBtBegin();    // Call in setup() — inits BLE stack + FIDO GATT server
void fidoBtUpdate();   // Call in loop() — handles timeout + completes BT-transport ops
void fidoBtCompleteOp(); // Send response for confirmed/declined BT request
