
!INCLUDE string.yh
!INCLUDE uart.yh


impl strlen: fn(str: [*]char) returns int8 {
    var len: int8 = 0;
    loop {
        if (str[len] eq 0) {
            break;
        }
        len += 1;
    }
    return len;
}

impl strcmp: fn(str1: [*]char, str2: [*]char) returns int8 {
    var i: int8 = 0;
    loop {
        if (str1[i] eq 0 and str2[i] eq 0) {
            return 0;
        }
        if (str1[i] ne str2[i]) {
            return @cast(int8 ,str1[i]) - @cast(int8, str2[i]);
        }
        i += 1;
    }
    return 0;
}

impl print: fn(str: [*]char) {
    var i: int8 = 0;
    loop {
        if (str[i] eq 0) {
            break;
        }
        uart_putc(str[i]);
        i += 1;
    }

    return;
}


impl println: fn(str: [*]char) {
    var i: int8 = 0;
    loop {
        if (str[i] eq 0) {
            break;
        }
        uart_putc(str[i]);
        i += 1;
    }
    uart_putc('\n');
    return;
}

impl readstr: fn(str: [*]char) {
    var c: char = 0;
    var i: int8 = 0;
    loop {
        c = uart_getc();
        uart_putc(c);
        if (c eq '\n' or i gt 127) {
            break;
        }
        str[i] = c;
        i += 1;
    }
    str[i] = 0;
    return;
}
