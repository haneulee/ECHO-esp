#pragma once

#include "EchoState.h"

// =====================================================
// DEVICE HELPERS
// =====================================================

int findDevice(String name);

int freeSlot();

void clearDevice(int i);

void clearAllTrackedEchoPeers();

// =====================================================
// BLE SCAN RESTORE
// =====================================================

void resumeEchoAdvertising();

void restoreEchoBlePeerScan(
  const char* reason
);

// =====================================================
// CLEANUP
// =====================================================

void cleanupDevices();

// =====================================================
// BLE SETUP
// =====================================================

void setupBLE();