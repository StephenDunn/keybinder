# keybinder
A software solution to fix mechanical keyboard double-typing issues using libevdev input filtering.
Specifically designed for mechanical keyboards like my Logitech G613, this tool helps prevent unwanted double key presses.

# Pre-requisites
libevdev

```
sudo pacman -S libevdev
```

# Build
```
gcc $(pkg-config --cflags --libs libevdev) -o keybinder keybinder.c
```


# Run
sudo ./keybinder /dev/input/event9 # Event 9 for my keyboard only.
