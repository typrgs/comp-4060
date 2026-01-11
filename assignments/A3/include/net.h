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
  MSG_BROADCAST,
  MSG_COMMAND
};
typedef enum MSG_TYPE MessageType;

// on broadcast the sensorID is a masked value indicating which data items are valid
#define TEMPERATURE_DATA_MASK 0x80
#define HUMIDITY_DATA_MASK    0x40
#define ANALOG_DATA_MASK      0x20
#define FAN_RPM_DATA_MASK     0x10
#define MAGNET_DISTANCE_MASK  0x08
#define RESERVED1_DATA_MASK   0x04
#define RESERVED2_DATA_MASK   0x02
#define RESERVED3_DATA_MASK   0x01

// offset into message data to find specific data when broadcasting; data always placed starting at the specific location
// the number of bytes of data is defined by the next offset value (up to the end of the buffer)
// NOTE: replies to specific sensor data requests always start at location 0 in the data
#define TEMPERATURE_OFFSET     0
// we *can't* have humidity and fan RPM in the same message, very much like J1939, to avoid having even larger messages
#define HUMIDITY_OFFSET        4
#define FAN_RPM_OFFSET         4
#define ANALOG_OFFSET          8
#define MAGNET_DISTANCE_OFFSET 10

struct PACKET
{
  uint8_t deviceID; // the identifier of the sensor module that is sending this message (or target if it's a command msg)
  uint8_t typeID;   // the identifier indicating if this is a request, reply, or broadcast
  uint8_t sensorID; // the identifier of the sensor requested or which sensors have valid data in the broadcast
  uint8_t data[PROTOCOL_DATA_BYTES];
};
typedef struct PACKET Packet;
