# FlashSim - Guanzhou's Fork

This is Guanzhou's fork of the FlashSim event-driven flash SSD simulator.


## Installation & Usage

### Prerequisites

- C++ Boost library: `sudo apt install libboost-all-dev`

### Installation

Clone the repo:

```bash
$ git clone git@github.com:josehu07/flashsim.git
$ cd flashsim
```

Folder structure:

```text
--- SSD/    # SSD simulator main logic
 |- FTL/    # FTL algorithms are separated from main logic, there are several provided by Matias here
 |- run/    # Example test runs
 |- Makefile
 |- COPYING
```

Run `make` to compile all provided test executables, and try some of them:

```bash
$ make
$ ./test2
$ make clean
```

### Usage

For use with your own projects, take this simulator as a library and call the `Ssd::event_arrive()` API. Follow provided test runs as a guidance.

1. Tweak SSD device configurations in `ssd.conf`
2. In your main file, call `Ssd::` class public APIs to simulate operating over a flash SSD
3. Compile your project & run


## FTL Contribution From Matias

FTL algorithms under `FTL/` folder provide BAST, FAST and DFTL implementation.

Please reference if you use for your research:

```bibtex
    @article{extendedflashsim,
    Author = {Matias Bj{\o}rling},
    Title = {Extended FlashSim},
    Url = {https://github.com/MatiasBjorling/flashsim},
    Year = 2011}
```


## Original README

Original FlashSim work is by Brendan Tauras btauras, Youngjae Kim, & Aayush Gupta at Pennsylvania State University.

```text
Copyright 2009, 2010 Brendan Tauras
btauras<code>at</code>gmail<code>dot</code>com

README is part of FlashSim.

FlashSim is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
any later version.

FlashSim is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with FlashSim.  If not, see <http://www.gnu.org/licenses/>.

##############################################################################

README

To use FlashSim with a main file, use the "ssd" (default) make target
and link the object files with your code that contains a main function.  Some
 examples can be found in the "test" and "trace" make targets and corresponding
source files.

Users must provide their FTL scheme to run FlashSim, which should include a
FTL, wear-leveler, and garbage-collector class.  Please note that FLASHSIM WILL
NOT WORK WITHOUT PROVIDING A FTL SCHEME.  The UML diagram has been provided to
assist with FTL development.  Many private functions of the Controller class
made available to the FTL class will assist users in FTL development.  Users can
run the "test" and "trace" make target output binaries to perform basic
testing of their FTL schemes.

Before running the FlashSim module, users should update the configuration file
"ssd.conf" according to their specificiations.  Descriptions of configuration
settings can be found in the sample configuration file.

FlashSim was designed to be capable of being modularly integrated with Disksim.
The Ssd::event_arrive() function signature in ssd_ssd.cpp was designed to match
the event_arrive() function signature that Disksim uses to send events to disks.

Any questions, comments, suggestions, or code additions are welcome.
```
