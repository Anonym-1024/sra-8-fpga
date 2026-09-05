

module ControlUnit (
    input wire clk,
    input wire [7:0] bus_in,
    input wire [3:0] flags_in
);

    wire [3:0] step;
    wire [3:0] cond;
    wire [7:0] opcode;
    wire [3:0] arg1;
    wire [3:0] arg2;
    wire [3:0] arg3;
    wire [7:0] imm0;
    wire [7:0] imm1;






    wire ir_frame0_w;
    wire ir_frame1_w;
    wire ir_frame2_w;
    wire ir_frame3_w;

    UC uc (
        .clk(clk),
        .reset(uc_reset),
        .step(step)
    );

    InstructionRegister ir (
        .clk(clk),

        .bus_in(bus_in),

        .write_en_0(ir_frame0_w),
        .write_en_1(ir_frame1_w),
        .write_en_2(ir_frame2_w),
        .write_en_3(ir_frame3_w),

        .cond_out(cond),
        .opcode_out(opcode),
        .arg1_out(arg1),
        .arg2_out(arg2),
        .arg3_out(arg3),
        .imm0_out(imm0),
        .imm1_out(imm1)

    );
    
endmodule