define restart
  mon reset halt
end

define rerun
  mon reset halt
  c
end

target extended-remote :3333

set print pretty
set print asm-demangle on
set mem inaccessible-by-default off
set pagination off
compare-sections
b main

load
