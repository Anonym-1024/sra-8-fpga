
// UART receiver, 8N1

module UartRx #(
    parameter DIV = 1250        // clk / baud = 12 MHz / 9600
) (
    input wire clk,
    input wire global_reset,

    input wire rx,

    output reg [7:0] data_out = 0,
    output reg valid = 0        // high for one clk when data_out holds a new byte
);

    reg [1:0] sync = 2'b11;     // rx is asynchronous to clk
    reg [7:0] shift = 0;
    reg [3:0] bits = 0;         // bits left to sample, 0 = idle
    reg [10:0] div = 0;

    always @(posedge clk) begin
        sync <= {sync[0], rx};
        valid <= 0;

        if (bits == 0) begin
            if (sync[1] == 0) begin         // start bit
                bits <= 10;
                div <= DIV / 2;             // sample in the middle of each bit
            end
        end else if (div == DIV - 1) begin
            div <= 0;
            bits <= bits - 1;
            if (bits == 10) begin
                if (sync[1] == 1)           // glitch, not a start bit
                    bits <= 0;
            end else if (bits == 1) begin
                if (sync[1] == 1) begin     // stop bit has to be high
                    valid <= 1;
                    data_out <= shift;
                end
            end else
                shift <= {sync[1], shift[7:1]};
        end else
            div <= div + 1;

        if (global_reset == 1) begin
            valid <= 0;
            bits <= 0;
        end
    end

endmodule
