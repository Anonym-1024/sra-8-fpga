

// Page table base register

module PTBR (
    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    output wire [15:0] addr_out,

    input wire byte_sel,
    input wire write_en,
    input wire read_en
    
);
    
    reg [7:0] byte_0 = 0;
    reg [7:0] byte_1 = 0;

    assign bus_out = (read_en == 1) ? (byte_sel == 0) ? byte_0 : byte_1 : 8'b0;

    assign addr_out = {byte_1, byte_0};

    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (byte_sel == 0 && write_en == 1)
                byte_0 <= bus_in;
            if (byte_sel == 1 && write_en == 1)
                byte_1 <= bus_in;
        end

        if (global_reset == 1)
            {byte_1, byte_0} <= 0;
    end

endmodule
