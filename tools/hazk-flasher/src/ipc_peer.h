// SPDX-License-Identifier: Apache-2.0
//
// The peer of the STM32 for the IPC protocol.
//
// Note: this code exercises the protocol from the edge MCU. It shows the
// credits, the error codes and the push frames on the USB console.

#ifndef HAZK_IPC_PEER_H
#define HAZK_IPC_PEER_H

#include <Arduino.h>

#include <stdbool.h>
#include <stddef.h>

// Start the peer. The function sets the port to the rate of the protocol.
void ipcBegin(unsigned long baud);

// Stop the peer, and give the port back to the bridge.
void ipcEnd();

bool ipcActive();

// Give one line of the console to the peer.
//
// Note: the caller removes the word "ipc" and the space after it.
void ipcCommand(const char *args);

// Read the port, and report the frames. Call this function from loop().
void ipcPoll();

void ipcHelp();

// Run a command, and give its output as text. A caller over the network uses
// this function.
String ipcRun(const char *args);

#endif  // HAZK_IPC_PEER_H
