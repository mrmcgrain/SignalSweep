// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Arduino.h>
#ifdef SIGNALSWEEP_CARDPUTER_ADV
void cardputerUiInit();
void cardputerUiTick();
void cardputerUiPrepareCapture();
bool cardputerHandleUsbCommand(const String& command);
#else
inline void cardputerUiInit() {}
inline void cardputerUiTick() {}
inline bool cardputerHandleUsbCommand(const String&) { return false; }
#endif
