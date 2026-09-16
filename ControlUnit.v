

module ControlUnit (
    input wire clk,
    input wire [1:0] clk_phase,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    input wire [3:0] flags_in,
    input wire int_in,

    output wire gr_read,
    output wire gr_write,
    output wire [3:0] gr_read_sel,
    output wire [3:0] gr_write_sel,
    output wire alu_read,
    output wire alu_op1_write,
    output wire alu_op2_write,
    output wire [3:0] alu_opcode,
    output wire mem_read,
    output wire mem_write,
    output wire ptbr_read,
    output wire ptbr_write,
    output wire ptbr_addr_read,
    output wire byte_sel,
    output wire pter_write,
    output wire pter_addr_read,
    output wire mar_write,
    output wire mar_addr_read,
    output wire pc_read,
    output wire pc_write,
    output wire pc_inc,
    output wire intpc_read,
    output wire intpc_write,
    output wire intpc_inc,
    output wire psr_read,
    output wire psr_write,
    output wire psr_flags_write,
    output wire intr_read,
    output wire intr_write,
    output wire svc
);




    wire gr_read_sel_arg1;
    wire gr_read_sel_arg2;
    wire gr_read_sel_arg3;

    wire imm_read;

    wire ir_frame0_write;
    wire ir_frame1_write;
    wire ir_frame2_write;
    wire ir_frame3_write;



    wire [4:0] step;
    wire [3:0] cond;
    wire [6:0] opcode;
    wire [3:0] arg1;
    wire [3:0] arg2;
    wire [3:0] arg3;
    wire [7:0] imm0;
    wire [7:0] imm1;



    wire uc_reset;

    UC uc (
        .clk(clk),
        .clk_phase(clk_phase),
        .reset(uc_reset),
        .step(step)
    );

    InstructionRegister ir (
        .clk(clk),
        .clk_phase(clk_phase),

        .bus_in(bus_in),

        .write_en_0(ir_frame0_write),
        .write_en_1(ir_frame1_write),
        .write_en_2(ir_frame2_write),
        .write_en_3(ir_frame3_write),

        .cond_out(cond),
        .opcode_out(opcode),
        .arg1_out(arg1),
        .arg2_out(arg2),
        .arg3_out(arg3),
        .imm0_out(imm0),
        .imm1_out(imm1)

    );

    assign bus_out = (imm_read == 1) ? (byte_sel == 0) ? imm0_out : imm1_out : 8'b0;


    wire z = flags_in[3];
    wire n = flags_in[2];
    wire c = flags_in[1];
    wire v = flags_in[0];


    wire cond_al  = 1'b1;          // always
    wire cond_eq  =  z;       
    wire cond_mi  =  n;   
    wire cond_vs  =  v;            // overflow set  
    wire cond_su  = ~c;            // smaller unsigned          (CC)
    wire cond_gu  =  c & ~z;       // greater unsigned
    wire cond_ss  =  (v ^ n);      // smaller signed        V = ~N
    wire cond_gs  = ~(v ^ n) & ~z; // greater signed

    wire cond_nvr = 1'b0;
    wire cond_ne  = ~z;            // not equal / zero clear    (ZC)
    wire cond_pl  = ~n;            // positive or zero
    wire cond_vc  = ~v;    
    wire cond_geu =  c;            // greater or equal unsigned (CS)
    wire cond_seu = ~c |  z;       // smaller or equal unsigned
    wire cond_ges = ~(v ^ n);      // greater or equal signed   V = N
    wire cond_ses =  (v ^ n) | z; // smaller or equal signed

    // --- select the one addressed by cond ---
    wire [15:0] cond_lut = {
        cond_al,
        cond_eq,
        cond_mi,
        cond_vs,
        cond_su,
        cond_gu,
        cond_ss,
        cond_gs,
        cond_nvr,
        cond_ne,
        cond_pl,
        cond_vc,
        cond_geu,
        cond_seu,
        cond_ges,
        cond_ses
    }; 

    wire cond_met = cond_lut[cond];

    wire [4:0] fetch_len = (int_in == 0) ? 12 : 10;
    reg [15:0] fetch_virt_ucode [0:15];
    reg [15:0] fetch_phys_ucode [0:15];
    reg [15:0] instr_ucode [0:(1<<10)-1];


    wire [15:0] fetch_ucode;
    wire [15:0] ucode;

    assign fetch_ucode = (int_in == 0) ? fetch_virt_ucode[step] : fetch_phys_ucode[step];

    assign ucode = (step < fetch_len) ? fetch_ucode : instr_ucode[(step - fetch_len) | (opcode << 3)];

    assign uc_reset = ucode == 0;

    wire [3:0] mux1 = ucode[15:12];
    wire [4:0] mux2 = ucode[11:7];
    wire [2:0] mux3 = ucode[6:4];
    wire [1:0] mux4 = (alu_read == 0) ? ucode[3:2] : 0;
    wire [1:0] mux5 = (alu_read == 0) ? ucode[1:0] : 0;
    

    assign gr_read = mux1 == 1;
    assign alu_read = mux1 == 2;
    assign mem_read = mux1 == 3;
    assign imm_read = mux1 == 4;
    assign pc_read = mux1 == 5;
    assign intpc_read = mux1 == 6;
    assign psr_read = mux1 == 7;
    assign ptbr_read = mux1 == 8;
    assign intr_read = mux1 == 9;

    assign gr_write = mux2 == 1;
    assign alu_op1_write = mux2 == 2;
    assign alu_op2_write = mux2 == 3;
    assign mem_write = mux2 == 4;
    assign pc_write = mux2 == 5;
    assign intpc_write = mux2 == 6;
    assign psr_write = mux2 == 7;
    assign ptbr_write = mux2 == 8;
    assign intr_write = mux2 == 9;
    assign ir_frame0_write = mux2 == 10;
    assign ir_frame1_write = mux2 == 11;
    assign ir_frame2_write = mux2 == 12;
    assign ir_frame3_write = mux2 == 13;
    assign mar_write = mux2 == 14;
    assign pter_write = mux2 == 15;

    assign pc_inc = mux3 == 1;
    assign intpc_inc = mux3 == 2;
    assign svc = mux3 == 3;
    assign psr_flags_write = mux3 == 4;
    assign byte_sel = mux3 == 5;

    assign gr_read_sel_arg1 = mux4 == 1;
    assign gr_read_sel_arg2 = mux4 == 2;
    assign gr_read_sel_arg3 = mux4 == 3;

    assign mar_addr_read = mux5 == 1;
    assign ptbr_addr_read = mux5 == 2;
    assign pter_addr_read = mux5 == 3;

    assign alu_opcode = ucode[3:0];

    assign gr_read_sel = gr_read_sel_arg1 ? arg1 :
                        gr_read_sel_arg2 ? arg2 :
                        gr_read_sel_arg3 ? arg3 : 0;
    
    assign gr_write_sel = arg1;

    

    initial begin
        $readmemh("control_rom.mem",instr_ucode);
        $readmemh("fetch.mem",fetch_virt_ucode);
        $readmemh("fetch2.mem",fetch_phys_ucode);
    end
    
endmodule