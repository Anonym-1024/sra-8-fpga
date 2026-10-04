// sudo picocom /dev/ttyUSB1 -b 115200 --omap crlf --imap lfcrlf


!INCLUDE uart.yh
!INCLUDE string.yh



var str: [64]char = {0, _};

@internal
impl echo: fn() {
    loop {
        readstr(@ptr(str));
        if (strcmp(@ptr(str), s"") eq 0) {
            break;
        }
        println(@ptr(str));
    }
}



@internal
impl strtou16: fn(strin: [*]char) returns uint16 {
    var num: uint16 = 0;
    var i: uint8 = 0;
    loop {
        if (strin[i] eq 0) {
            break;
        }
        num = num * 10 + @cast(uint16)(strin[i] - '0');

        i += 1;
    }
    return num;
}

@internal
impl u16tostr: fn(num: uint16, strout: [*]char) {
    var i: uint16 = 0;
    loop {
        strout[i] = '0' + @cast(char)(num % 10);
        num /= 10;
        i += 1;
        if (num eq 0) {
            break;
        }
    }
    strout[i] = 0;

    // Reverse the generated digits in place
    var len: uint16 = i;
    var j: uint16 = 0;
    loop {
        if (j eq len / 2) {
            break;
        }
        var temp: char = strout[j];
        strout[j] = strout[len - 1 - j];
        strout[len - 1 - j] = temp;
        j += 1;
    }
}

@internal
impl sum: fn() {
    var suma: uint16 = 0;
    loop {
        readstr(@ptr(str));
        if (strcmp(@ptr(str), s"") eq 0) {
            break;
        }
        var num: uint16 = strtou16(@ptr(str));
        suma += num;

    }

    u16tostr(suma, @ptr(str));

    println(@ptr(str));
}


@internal
impl recognize_command: fn(strin: [*]char) {
    if (strcmp(strin, s"echo") eq 0) {
        echo();
    } else if (strcmp(strin, s"sum") eq 0) {
        sum();
    }
}



@main
impl main: fn() {

    loop {
        readstr(@ptr(str));
        recognize_command(@ptr(str));
    }
}
