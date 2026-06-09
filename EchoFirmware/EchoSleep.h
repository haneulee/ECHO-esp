#pragma once

// =====================================================
// SLEEP MODE (push button toggle)
// =====================================================

void setupSleepButton();

// Returns true when waking from light sleep (skip one loop tick).
bool processSleepMode();

bool isSleepModeActive();
