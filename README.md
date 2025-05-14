# cfd-cli

## Getting Started

### fluid.c

```bash
gcc -Wall -W -pedantic -D_BSD_SOURCE -std=c99 -DG=1 -DP=4 -DV=4 fluid.c -o fluid -lm
./fluid < fluid.c
```

### Makefile Options

- View build settings
```bash
make info
```
- Basic compilation (Windows)
```bash
make clean && make
```
- Install system-wide (Linux/macOS)
```bash
sudo make install
```
- Uninstall system-wide (Linux/macOS)
```bash
sudo make uninstall
```
