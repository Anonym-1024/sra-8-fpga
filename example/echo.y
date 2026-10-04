

!INCLUDE uart.yh


@main
impl main: fn() {
    uart_putc('a');
    loop {
        var c: char = uart_getc();
        uart_putc(c);
    }
}
