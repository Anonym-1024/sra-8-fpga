

module MAR (
    input wire write_en_0,
    input wire write_en_1,
    input wire clk,
    input wire [7:0] bus_in,
    output wire [15:0] addr_out,
    output wire [7:0] byte_0_out,
    output wire [7:0] byte_1_out
);

    reg [7:0] byte_0;
    reg [7:0] byte_1;

    assign addr_out = {byte_1, byte_0};

    

    assign byte_0_out = byte_0;
    assign byte_1_out = byte_1;

    always @(posedge clk) begin
        if (write_en_0 == 1)
            byte_0 <= bus_in;
        if (write_en_1 == 1)
            byte_1 <= bus_in;
    end
    
endmodule