

module ControlUnit (
    input wire clk,
    input wire [7:0] bus_in,
    input wire [3:0] flags_in,
    input wire int_in,

    output wire gr_read,
    output wire gr_write,
    output wire [3:0] gr_read_sel,
    output wire [3:0] gr_write_sel,
    output wire alu_read_en,
    output wire alu_write_op1,
    output wire alu_write_op2,
    output wire [3:0] alu_opcode,
    output wire mem_read,
    output wire mem_write,
    output wire ptbr_read_byte0,
    output wire ptbr_read_byte1,
    output wire ptbr_write_byte0,
    output wire ptbr_write_byte1,
    output wire ptbr_addr_read,
    output wire byte_sel,
    output wire pter_write_byte0,
    output wire pter_write_byte1,
    output wire pter_addr_read,
    output wire mar_write_byte0,
    output wire mar_write_byte1,
    output wire mar_addr_read,
    output wire pc_read_byte0,
    output wire pc_read_byte1,
    output wire pc_write_byte0,
    output wire pc_write_byte1,
    output wire pc_inc,
    output wire intpc_read_byte0,
    output wire intpc_read_byte1,
    output wire intpc_write_byte0,
    output wire intpc_write_byte1,
    output wire intpc_inc,
    output wire psr_read,
    output wire psr_write,
    output wire psr_flags_write,
    output wire intr_read,
    output wire intr_write,
    output wire svc
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