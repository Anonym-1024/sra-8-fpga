
// Instruction register

module InstructionRegister (
    input wire clk,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,

    output wire [3:0] cond_out,
    output wire [6:0] opcode_out,
    output wire [3:0] arg1_out,
    output wire [3:0] arg2_out,
    output wire [3:0] arg3_out,
    output wire [7:0] imm0_out,
    output wire [7:0] imm1_out,

    input wire write_en_0,
    input wire write_en_1,
    input wire write_en_2,
    input wire write_en_3



);


    reg [31:0] instruction = 0;

    assign cond_out = instruction[31:28];
    assign opcode_out = instruction[26:20];
    assign arg1_out = instruction[19:16];
    assign arg2_out = instruction[15:12];
    assign arg3_out = instruction[11:8];
    assign imm0_out = instruction[7:0];
    assign imm1_out = instruction[15:8];


    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (write_en_0 == 1)
                instruction[31:24] <= bus_in;
            if (write_en_1 == 1)
                instruction[23:16] <= bus_in;
            if (write_en_2 == 1)
                instruction[15:8] <= bus_in;
            if (write_en_3 == 1)
                instruction[7:0] <= bus_in;
        end
    end

endmodule
