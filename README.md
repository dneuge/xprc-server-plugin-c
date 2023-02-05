# X-Plane Remote Control

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)

XPRC provides an easy way to interact with X-Plane datarefs and commands from other applications/computers via a TCP network connection.

This project currently contains:

- server implementations: (X-Plane plugins)
  - [server/python3](server/python3/): implementation using [XPPython3](https://xppython3.readthedocs.io/) (incomplete)
  - [server/native-c](server/native-c/): native plugin written in C (incomplete)

It will also contain at some point:

- the protocol specification
- a Java client library

## License

All parts of this project are provided under [MIT license](LICENSE.md).
