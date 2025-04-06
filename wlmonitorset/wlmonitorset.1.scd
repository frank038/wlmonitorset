wlsunset(1)

# NAME

wlmonitorset - day/night gamma adjustments for Wayland compositors supporting
wlr-gamma-control-unstable-v1

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
	Set very low temperture - opional (default 0: not used).

*-S* <sunrise>
	Manual time for sunrise as HH:MM (default 08:00).

*-s* <sunset>
	Manual time for sunset as HH:MM (default 18:00).

*-M* <long>
	Manual time for dusk as HH:MM - optional (e.g. 21:30).

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
    1 linear interpolation; 2 previous behaviour.
    Use the data in the data_array file as starting curve,
    that becomes the default starting rgb values for the monitor.
    The data_array file is a text file containing three rows, one per colour,
    newline terminated.
    Each row has 256 values, float numbers from 0.0 to 1.0.
    If used, the next colour corrections will be applied on to this curve.
    The gamma option has no effect with this option.

*-b <value>*
    Set the brightness globally. From 0.3 to 1.0.

*-B <value:value:value>*
    Set the brightness for sunrise, sunset and dusk. From 0.3 to 1.0.

The value 6500 is the neutral value, no colours corrections.
The data_array file can be placed in HOME/.config/wlmonitorset folder
or in this program folder.


# EXAMPLE

```
# With night light only options.
wlmonitorset -T 6500 -t 4000 -S 08:00 -s 18:00 
```


# AUTHORS

This work is from wlsunset, maintained by Kenny Levinsen
