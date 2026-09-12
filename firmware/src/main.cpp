// ===========================================================================
//  main.cpp - Step 2.3 layer 2: the thin conductor
//
//  Notice how small this is now. All the serial-handling detail moved into
//  the HostLink module. main.cpp's whole job is to create the pieces and
//  keep calling them - it reads like a summary of what the firmware does,
//  not a pile of low-level detail. That is the payoff of splitting code into
//  modules.
// ===========================================================================

#include "HostLink.h" // our host-communication module

// The single HostLink object for the whole program. Declared here, outside
// any function, so it lives for the entire run (not just one loop pass).
HostLink host;

void setup()
{
  host.begin(); // open the serial link, once
}

void loop()
{
  host.poll(); // check for and handle any incoming line - never blocks

  // Later, other modules' update() calls will sit here alongside host.poll(),
  // each taking its turn on every pass, none of them blocking the others.
}
