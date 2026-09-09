

module Memory (
    input wire read_en,
    input wire write_en,
    input wire clk,
    input wire [15:0] addr,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out
);


    SPRAM spram (
        .clk(clk),
        .we(write_en),
        .addr(addr[13:0]),
        .data_in(bus_in),
        .data_out(selected)
    );

    reg [7:0] selected;

    assign bus_out = (read_en == 1) ? selected : 0;

    
    
endmodule