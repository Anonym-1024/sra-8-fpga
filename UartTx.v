
// UART transmitter, 8N1

module UartTx #(
    parameter DIV = 1250        // clk / baud = 12 MHz / 9600
) (
    input wire clk,

    input wire [7:0] data_in,
    input wire start,

    output reg tx = 1
);

    reg [8:0] shift = 0;
    reg [3:0] bits = 0;         // bits left to send, 0 = idle
    reg [10:0] div = 0;

    always @(posedge clk) begin
        if (bits == 0) begin
            if (start == 1) begin
                shift <= {1'b1, data_in};   // data, then the stop bit
                tx <= 0;                    // start bit
                bits <= 10;
                div <= 0;
            end
        end else if (div == DIV - 1) begin
            div <= 0;
            tx <= shift[0];
            shift <= {1'b1, shift[8:1]};
            bits <= bits - 1;
        end else
            div <= div + 1;
    end

endmodule
