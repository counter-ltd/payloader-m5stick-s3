#pragma once
#include "menu_screen.h"

// Call once in setup()
void irTabSetup();

// Call every loop() — polls for IR signals when reader is active
void irTabUpdate();

// The MenuTab to pass into the main MenuScreen
extern MenuTab irMenuTab;
