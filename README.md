# kbdbacklight
kbdbacklight is a Linux tool to adjust keyboard backlight illumination to the
specified brightness [0..100%].

Copyright (C) 2018 Andreas Gollsch

This program comes with ABSOLUTELY NO WARRANTY.
This is free software, and you are welcome to redistribute it under certain
conditions.

This project is born out of the situation that there is no tool to control the
keyboard backlight illumination on my MacBook Air with usability like
xbacklight does it for the screen backlight.

The other programs I found are using dbus or need root permission or they are
constrained to a specific keyboard led device.

This program should be able to detect the correct keyboard led device in your
notebook. If not please let me know.

## Build & Installation
You can build the project with
```
make
```
If you use systemd then you can install the program and service with
```
make install
```
After installation systemd should start the server instance of kbdbacklight.
You can check the status with
```
systemctl status kbdbacklight
```
If you prefer not to use systemd than you can start kbdbacklight as daemon
(root permission required) with
```
kbdbacklight -B
```

If kbdbacklight service/daemon is running you can use
```
kbdbacklight
```
to see the actual brightness in percent.

## Examples
Set the brighness to 50%
```
kbdbacklight -s50
```
Decrement by 20%
```
kbdbacklight -d20
```
Increment by 15%
```
kbdbacklight -i15
```
Turn keyboard backlight off
```
kdbbacklight -o
```
Set backlight to maximum
```
kbdbacklight -m
```
## License
This project is licensed under GNU GPL - see [LICENSE](LICENSE) for
details
