// TODO: Clean up the add logic, remove carry
// Memory address register
//
// Besides being loaded from the bus, MAR can add the bus to itself, one
// byte at a time (add_en with byte_sel): the low byte keeps its carry in
// add_carry, which the high byte adds.  ldo, sto, ldi, sti and lea form
// rBa + offset this way, without the ALU and without touching the flags.
// read_en puts a MAR byte (byte_sel) on the bus: the untranslated address,
// for the base write-back of ldi / sti and the result of lea.

module MAR (

    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    output wire [15:0] addr_out,
    output wire [7:0] byte_0_out,
    output wire [7:0] byte_1_out,

    input wire write_en,
    input wire read_en,
    input wire add_en,
    input wire byte_sel
);

    reg [7:0] byte_0 = 0;
    reg [7:0] byte_1 = 0;
    reg add_carry = 0;

    assign addr_out = {byte_1, byte_0};

    assign byte_0_out = byte_0;
    assign byte_1_out = byte_1;

    assign bus_out = (read_en == 1) ? ((byte_sel == 0) ? byte_0 : byte_1) : 8'b0;

    wire [8:0] sum_0 = {1'b0, byte_0} + {1'b0, bus_in};
    wire [7:0] sum_1 = byte_1 + bus_in + {7'b0, add_carry};

    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (byte_sel == 0 && write_en == 1)
                byte_0 <= bus_in;
            if (byte_sel == 1 && write_en == 1)
                byte_1 <= bus_in;
            if (byte_sel == 0 && add_en == 1) begin
                byte_0 <= sum_0[7:0];
                add_carry <= sum_0[8];
            end
            if (byte_sel == 1 && add_en == 1)
                byte_1 <= sum_1;
        end

        if (global_reset == 1) begin
            {byte_1, byte_0} <= 0;
            add_carry <= 0;
        end
    end

endmodule
