# Project Title

Stella CAN Read software for Linux

## Getting Started

First ensure you clone/download this repo on a Linux machine that has g++ installed.

### Prerequisites

g++ with C++17 and lpthread

canplayer tool

### Installing

Go into the user_src folder, open a terminal and run:

`g++ -std=c++17 -Wall -W user_src_main.cxx socket_reader.cxx tcp_client.cxx data_handler.cxx sd_controller.cxx ft_client.cxx -o stella -lpthread`

Then open a new terminal and run 

```
$ sudo ip link add vcan0 type vcan
$ sudo ip link set up vcan0

$ canplayer -I logfile.scl vcan0=canx
```

With logfile.scl being a logfile of your choice

Then go back to the initial terminal and run:

`./stella`

### Modifying the build

Configuration options are available in the `stellaCANRead.h` file, with each having a short description on how it works


