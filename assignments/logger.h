#pragma once

// This unit configures and uses generic clock 10.

// allows messages to be logged to the USB port of the curiosity nano board
// requires a serial program on your computer (I use `minicom`, Unix only, and there are pure Windows apps)

// must be called first to initialize serial communications via the board CDC
void logInit();

// has standard printf() style usage with a string and a variable number of additional parameters
// it has limited functionality for formatting codes:
//    1. %s, as expected
//    2. %X.Yf, not as expected. 
//         - Fixed point values only. 
//         - X indicates number of digits to print.
//         - .Y is optional. If '.' is present Y *must* also be present. Y indicates number of digits after the decimal.
// WARNING: variable argument lists use non-reentrant code; concurrent use will cause unexpected results
void logMsg(const char *msg, ...);
