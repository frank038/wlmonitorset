# wlmonitorset
A night light program for wayland.

wlmonitorset
Command line night light application for wlroots compositors.
This work is from wlsunset.

In development and testing stage. Dusk and two curves option are in todo state.

How to build:

meson build
ninja -C build



wlsunset(1)

# NAME

wlmonitorset - day/night gamma adjustments for Wayland compositors supporting
wlr-gamma-control-unstable-v1. Can be used three periods of the day.

The -f 1 option has priority over the -T option.

# SYNOPSIS

*wlmonitorset* [options...]

# OPTIONS

*-h*
	Show this help message.

*-T* <temp>
	Set high temperature (default: 6500).

*-t* <temp>
	Set low temperature (default: 4500).

*-m* <temp>
	Set very low temperture - opional (default 0: not used) (to do).

*-S* <sunrise>
	Manual time for sunrise as HH:MM (default 08:00).

*-s* <sunset>
	Manual time for sunset as HH:MM (default 18:00).

*-M* <long>
	Manual time for dusk as HH:MM - optional (e.g. 21:30) (to do).

*-d* <duration>
	Manual animation time in seconds (default 60).
    The transition will be performed in ten steps.
    The transition starts from the time setted.
	Only applicable when using manual sunset/sunrise times.

*-g* <gamma>
	Set gamma (default: 1.0).
    Only applicable without the -f option.

*-o* <output>
    Name of output (display) to use (default: all)."

*-f <type>*
    1 sunrise only; 2 sunrise and sunset (to do).
    Use the data in the data_array files (data_array for sunrise, 
    data_array2 for sunset) as colour curves,
    that become/becomes the rgb values for the monitor.
    The data_array files are a text file containing three rows, 
    one per colour, newline terminated.
    Each row has 256 values, float numbers from 0.0 to 1.0.
    If used, the next colour corrections will be applied on to this curve.
    The gamma option has no effect with this option.

*-b <value>*
    Set the brightness globally. From 0.3 to 1.0. Do not use with -f,
    use -B instead to preserve the starting curve.

*-B <value:value:value>*
    Set the brightness for sunrise, sunset and dusk. From 0.3 to 1.0.
    If used with -f, make sure to use 1.0:value:value (or 1.0:1.0:b)
    to preserve the curves.

The value 6500 is the neutral value, no colours corrections.
For the three periods of the day, any temperature values can be used.
The data_array file can be placed in HOME/.config/wlmonitorset folder
or in this program folder.


# EXAMPLE

```
# With night light only options.
wlmonitorset -T 6500 -t 4000 -S 08:00 -s 18:00 
```

# CREATE_CURVE
In the folder create_curve is a helper program that let user create a smooth curve
to be used with wlmonitorset. Just compile it with the command: gcc main.c -lm -o create_curve 
and set some options, for example: ./create_curve -r 0.0:0.5:1.0 -g 0.0:0.5:1.0 -b 0.0:0.5:1.0 (in this case three values per colour channel; three flat curves - from 0.0 to 1.0 - will be implemented in the file data_array, that can be used by wlmonitorset if the case). The number of data per colour channel to pass as options must be 3 or 5 or 8. Limitations: the curves must be monotonic (the value of each point must be greater than the value of the previous one; their slope can be of any type).
