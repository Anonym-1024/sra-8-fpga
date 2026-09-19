
// Port: one UART, 8N1.  PTW sends a byte, PTR reads the last received byte.
// irq_out goes high when a byte has been received and low when it is read.

module Port (
    input wire clk,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire [7:0] bus_out,

    input wire uart_rx,
    output wire uart_tx,
    output wire irq_out,

    input wire port_write,
    input wire port_read
);

    wire [7:0] rx_data;
    wire rx_valid;

    reg irq = 0;
    assign irq_out = irq;

    reg [7:0] outgoing = 0;     // last byte sent, shown on the LEDs


    assign bus_out = port_read ? rx_data : 0;

    UartTx uart_transmitter (
        .clk(clk),

        .data_in(bus_in),
        .start(clk_phase == 2 && port_write),
        .tx(uart_tx)
    );

    UartRx uart_receiver (
        .clk(clk),

        .rx(uart_rx),
        .data_out(rx_data),
        .valid(rx_valid)
    );

    always @(posedge clk) begin
        if (clk_phase == 2 && port_write)
            outgoing <= bus_in;

        if (rx_valid == 1)
            irq <= 1;
        else if (clk_phase == 2 && port_read)
            irq <= 0;
    end

endmodule
