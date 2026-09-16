
// TODO: Page fault handling

// Page table entry register

module PTER (
    input wire clk,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire pf_out,
    output wire [15:0] addr_out,

    input wire write_en,
    input wire byte_sel
    
);
    
    reg [7:0] byte_0;
    reg [7:0] byte_1;

    assign addr_out = {byte_1, byte_0};


    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (byte_sel == 0 && write_en == 1)
                byte_0 <= bus_in;
            if (byte_sel == 1 && write_en == 1)
                byte_1 <= bus_in;
        end
    end

endmodule
