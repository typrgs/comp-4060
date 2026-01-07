#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

"""
based on the OpenOCD RPC example, covered by GNU GPLv3 or later and Copyright (C) 2014 Andreas Ortmann (ortmann@finf.uni-hannover.de)
"""

import socket
import subprocess
import time
import sys

class OpenOCD:
    COMMAND_TOKEN = '\x1a'
    def __init__(self, verbose=False):
        self.verbose = verbose
        self.tclRpcIp       = "127.0.0.1"
        self.tclRpcPort     = 6666
        self.bufferSize     = 4096

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, type, value, traceback):
        self.disconnect()

    def connect(self):
        self.sock.connect((self.tclRpcIp, self.tclRpcPort))

    def disconnect(self):
        try:
            self.send("exit")
        finally:
            self.sock.close()

    def send(self, cmd):
        """Send a command string to TCL RPC. Return the result that was read."""
        data = (cmd + OpenOCD.COMMAND_TOKEN).encode("utf-8")
        if self.verbose:
            print("<- ", data)

        self.sock.send(data)
        return self._recv()

    def _recv(self):
        """Read from the stream until the token (\x1a) was received."""
        data = bytes()
        while True:
            chunk = self.sock.recv(self.bufferSize)
            data += chunk
            if bytes(OpenOCD.COMMAND_TOKEN, encoding="utf-8") in chunk:
                break

        if self.verbose:
            print("-> ", data)

        data = data.decode("utf-8").strip()
        data = data[:-1] # strip trailing \x1a

        return data

if __name__ == "__main__":

    def show(*args):
        print(*args, end="\n\n")

    cmdLine = sys.argv[1:]
    numBytes = len(cmdLine)
    startAddr = int('000fe000', base=16)
    currByte = startAddr

    proc = subprocess.Popen(["openocd", "-f", "board/microchip_same51_curiosity_nano.cfg"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # wait to make sure the server is up and running; it takes a bit to handshake with the device
    time.sleep(.5)

    with OpenOCD() as ocd:
      ocd.send("capture \"halt\"")[:-1]

      print("existing flash contents:")
      show(ocd.send("flash mdb 0x{1:08x} 0x{0:02x}".format(numBytes, startAddr))[:-1])
      ocd.send("flash erase_sector 0 127 127")[:-1]

      print("confirming erased flash:")
      show(ocd.send("flash mdb 0x{1:08x} 0x{0:02x}".format(numBytes, startAddr))[:-1])

      # parse the command line with each item being the next byte to write as a parameter
      for byte in cmdLine: 
        ocd.send("flash fillb 0x{1:08x} 0x{0:02x} 1".format(int(byte), currByte))[:-1]
        # add a small delay to make sure the device is done before trying the next byte
        time.sleep(.5)
        currByte = currByte + 1

      print("parameters in flash:")
      show(ocd.send("flash mdb 0x{1:08x} 0x{0:02x}".format(numBytes, startAddr))[:-1])

      ocd.send("reset")

    time.sleep(.5)

    proc.terminate()
