#include "morse_map.h"

#define CHARSET_SIZE 26

static char morse[CHARSET_SIZE][MORSE_MAX_LEN + 1] = {
    ".-",    // A
    "-...",  // B
    "-.-.",  // C
    "-..",   // D
    ".",     // E
    "..-.",  // F
    "--.",   // G
    "....",  // H
    "..",    // I
    ".---",  // J
    "-.-",   // K
    ".-..",  // L
    "--",    // M
    "-.",    // N
    "---",   // O
    ".--.",  // P
    "--.-",  // Q
    ".-.",   // R
    "...",   // S
    "-",     // T
    "..-",   // U
    "...-",  // V
    ".--",   // W
    "-..-",  // X
    "-.--",  // Y
    "--.."   // Z
};

static char charset[CHARSET_SIZE] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'
};

static bool strCompare(char *a, char *b)
{
  bool result = true;
  int pos = 0;
  
  while(a[pos] != '\0' && b[pos] != '\0' && result)
  {
    if(a[pos] != b[pos])
    {
      result = false;
    }
    pos++;
  }

  if(result && ((a[pos] == '\0' && b[pos] != '\0') || (a[pos] != '\0' && b[pos] == '\0')))
  {
    result = false;
  }

  return result;
}

char binToChar(char *bin)
{
  bool found = false;
  char result = '\0';

  for(int i=0; i<CHARSET_SIZE && !found; i++)
  {
    if(strCompare(bin, morse[i]))
    {
      result = charset[i];
      found = true;
    }
  }

  return result;
}
