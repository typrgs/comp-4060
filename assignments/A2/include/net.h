#pragma once

#include "common.h"

// the protocol will ALWAYS transmit and receive 16 bytes -- any unused bytes MUST be set to 0 before tx
#define PROTOCOL_DATA_BYTES    13
// if no data is to be sent use the HEADER_BYTES to simply send the command/request and minimize bus usage
#define PROTOCOL_HEADER_BYTES  3 
#define PROTOCOL_PACKET_SIZE   (PROTOCOL_DATA_BYTES+PROTOCOL_HEADER_BYTES)

// ID to transmit if this message is for all devices
#define ALL_DEVICES   0xff

enum SENSORS
{
  TEMP_HUMIDITY,
  RAW_ANALOG,
  MAGNET_DISTANCE,  // using the LIN Hall sensor, with north pole pointing at sensor; reliable measurements within 1cm
  FAN_RPM,
  NUM_SENSORS
};
typedef enum SENSORS Sensor;

enum MSG_TYPE
{
  MSG_REQUEST,
  MSG_REPLY,
  MSG_COMMAND
};
typedef enum MSG_TYPE MessageType;

struct PACKET
{
  uint8_t deviceID; // the identifier of the sensor module that is sending this message (or target if it's a command msg)
  uint8_t typeID;   // the identifier indicating if this is a request or reply
  uint8_t sensorID; // the identifier of the sensor requested
  uint8_t data[PROTOCOL_DATA_BYTES];
};
typedef struct PACKET Packet;
