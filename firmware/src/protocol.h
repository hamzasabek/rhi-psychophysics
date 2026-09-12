// ===========================================================================
//  protocol.h - the CONTRACT between the Teensy and the host PC
//
//  This file is the single agreed description of what messages cross the USB
//  cable and what they mean. Both sides must obey it: the C++ firmware here,
//  and (later) the Python host, which will mirror this file.
//
//  It deliberately contains NO logic - only definitions and documentation.
//  A contract describes; it does not act.
//
//  MESSAGE FORMAT
//  --------------
//  Every message is one line of plain text, ended by a newline '\n'.
//  Plain text (rather than raw bytes) is chosen so a human can read and type
//  the messages directly in a serial monitor while debugging.
//
//  COMMANDS  (host -> Teensy)
//  --------------------------
//    PING <seq>
//        "Are you there?"  <seq> is any number the host chooses; the Teensy
//        will echo it back, so the host can match a reply to its request.
//
//  EVENTS / REPLIES  (Teensy -> host)
//  ----------------------------------
//    PONG <seq> t_us=<n>
//        Reply to PING. <seq> is the same number the host sent. <n> is the
//        Teensy's current clock reading in microseconds.
//
//    ERR <msg>
//        Something went wrong (e.g. an unrecognised command). <msg> is a
//        short human-readable description. The Teensy always stays running.
//
//  As the project grows, every new command and reply gets added HERE first,
//  and only then implemented. The contract leads; the code follows.
// ===========================================================================

#pragma once
// ^ include this file's contents only once per compiled file.

// We do not need any definitions here yet - the commands above are simple
// enough to recognise by their text. As the protocol grows we may add
// constants or enums to this file so both sides refer to the same names.
