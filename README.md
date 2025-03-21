# wlmonitorset
A night light program for wayland.

wlmonitorset
Command line night light application for wlroots compositors.
This work is from wlsunset.

In testing stage.

How to build and install:

meson build
ninja -C build
sudo ninja -C build install

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
	Set very low temperture (default 0: not used).

*-S* <sunrise>
	Manual time for sunrise as HH:MM (e.g. 06:30).

*-s* <sunset>
	Manual time for sunset as HH:MM (e.g. 18:30).

*-M* <long>
	Manual time for dusk as HH:MM (e.g. 21:30).

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

*-f 1* 
    Use the data in the data_array file as starting curve,
    that becomes the default starting rgb values for the monitor.
    The data_array file is a text file containing three rows per colour.
    Each row has 256 values, float numbers from 0.0 to 1.0.
    When used, the next colour corrections will be applied on to this curve.
    The gamma option has no effect with this option.
    (The curve(s) can be created following what is described in my create-icc program).


# EXAMPLE

```
# With night light only options.
wlmonitorset -T 6500 -t 4000 -S 08:00 -s 18:00 
```

Greater precision than one decimal place serves no purpose
(https://xkcd.com/2170/) other than padding the command-line.

# AUTHORS

This work is from wlsunset, maintained by Kenny Levinsen
(https://sr.ht/~kennylevinsen/wlsunset).

Important: 
The data_array file is a text file containing three rows, one per colour channel,
newline terminated.
