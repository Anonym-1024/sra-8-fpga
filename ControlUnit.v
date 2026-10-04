

module ControlUnit (
    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    input wire [1:0] pl_in,
    input wire [3:0] flags_in,

    input wire irq_in,
    input wire svc_in,
    input wire pf_in,
    input wire ini_in,
    output wire int_out,
    input wire irqm_in,

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

    output wire byte_sel,


    output wire ptbr_read,
    output wire ptbr_write,
    output wire ptbr_addr_read,

    output wire pter_write,
    output wire pter_addr_read,

    output wire mar_write,
    output wire mar_addr_read,
    output wire mar_read,
    output wire mar_add,

    output wire pc_read,
    output wire pc_write,
    output wire pc_inc,

    output wire intpc_read,
    output wire intpc_write,
    output wire intpc_inc,

    output wire psr_read,
    output wire psr_write,
    output wire psr_flags_write,

    output wire port_read,
    output wire port_write,

    output wire intr_read,
    output wire intr_write,
    output wire svc_out,

    // boot: copy of the boot ROM into memory
    input wire btc_done_in,
    output wire btrom_read,


    output reg led_normal,
    output reg led_interrupted,
    output reg led_boot
);


    // Immediate and offset read

    wire imm_read;
    wire off12_read;
    wire gr_write_base;


    // Instruction register

    wire ir_frame0_write;
    wire ir_frame1_write;
    wire ir_frame2_write;
    wire ir_frame3_write;


    wire [3:0] cond;
    wire [6:0] opcode;
    wire [3:0] arg1;
    wire [3:0] arg2;
    wire [3:0] arg3;
    wire [7:0] imm_byte0;
    wire [7:0] imm_byte1;
    wire [7:0] off12_byte0;
    wire [7:0] off12_byte1;


    // UC

    wire [4:0] step;
    wire uc_reset;





    // ------------------------- Condition logic ----------------------------- //


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

    wire cond_nvr = 1'b0;          // never
    wire cond_ne  = ~z;            // not equal / zero clear    (ZC)
    wire cond_pl  = ~n;            // positive or zero
    wire cond_vc  = ~v;            // overflow clear
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

    wire cond_met = cond_lut[15 - cond];


    // ------------------------- END OF SECTION ----------------------------- //












    // ------------------------- Control logic ----------------------------- //


    reg is_interrupted = 0;

    assign int_out = is_interrupted;

    reg [15:0] fetch_ucode [0:15];
    reg [15:0] instr_ucode [0:(1<<11)-1];   // {opcode, step[3:0]}: 16 steps per opcode

    reg [15:0] ucode = 0;

    // Control word: [15:11] MUX 1, [10:6] MUX 2, [5:3] MUX 3, [2] unused,
    // [1:0] MUX 5.  A GR read names its register field in MUX 1 (codes
    // 1 ... 3), an ALU read its operation (codes 16 ... 27: alu_opcode =
    // code - 16).
    wire [4:0] mux1 = ucode[15:11];
    wire [4:0] mux2 = ucode[10:6];
    wire [2:0] mux3 = ucode[5:3];
    wire [1:0] mux5 = ucode[1:0];


    wire xpc_read;
    wire xpc_write;
    wire xpc_inc; //

    assign gr_read = mux1 == 1 | mux1 == 2 | mux1 == 3;
    assign mem_read = mux1 == 4;
    assign imm_read = mux1 == 5;
    assign pc_read = mux1 == 6 | (is_interrupted == 0 & xpc_read == 1); //
    assign intpc_read = mux1 == 7 | (is_interrupted == 1 & xpc_read == 1); //
    assign xpc_read = mux1 == 8;
    assign psr_read = mux1 == 9;
    assign ptbr_read = mux1 == 10;
    assign intr_read = mux1 == 11;
    assign port_read = mux1 == 12;
    assign btrom_read = mux1 == 13;
    assign off12_read = mux1 == 14;
    assign mar_read = mux1 == 15;
    assign alu_read = mux1[4] == 1;

    assign gr_write = mux2 == 1 | gr_write_base;
    assign alu_op1_write = mux2 == 2;
    assign alu_op2_write = mux2 == 3;
    assign mem_write = mux2 == 4;
    assign pc_write = (mux2 == 5) | (is_interrupted == 0 & xpc_write == 1); //
    assign intpc_write = (mux2 == 6) | (is_interrupted == 1 & xpc_write == 1); //
    assign xpc_write = mux2 == 7;
    assign psr_write = mux2 == 8;
    assign ptbr_write = mux2 == 9;
    assign intr_write = mux2 == 10;
    assign ir_frame0_write = mux2 == 11;
    assign ir_frame1_write = mux2 == 12;
    assign ir_frame2_write = mux2 == 13;
    assign ir_frame3_write = mux2 == 14;
    assign mar_write = mux2 == 15;
    assign pter_write = mux2 == 16;
    assign port_write = mux2 == 17;
    assign mar_add = mux2 == 18;
    assign gr_write_base = mux2 == 19;

    assign xpc_inc = mux3 == 1;
    assign pc_inc = is_interrupted == 0 & xpc_inc == 1;
    assign intpc_inc = is_interrupted == 1 & xpc_inc == 1;
    assign svc_out = mux3 == 2;
    assign psr_flags_write = mux3 == 3;
    assign byte_sel = mux3 == 4;
    assign uc_reset = mux3 == 5;

    // Address translation is off at privilege level 0 and while interrupted:
    // x_addr_read then addresses memory through MAR instead of PTER
    wire phys = pl_in == 0 | is_interrupted == 1;
    wire x_addr_read = mux5 == 1;

    assign mar_addr_read = x_addr_read == 1 & phys == 1;
    assign ptbr_addr_read = mux5 == 2;
    assign pter_addr_read = x_addr_read == 1 & phys == 0;

    assign alu_opcode = mux1[3:0];

    assign gr_read_sel = (mux1 == 1) ? arg1 + byte_sel :
                         (mux1 == 2) ? arg2 + byte_sel :
                         (mux1 == 3) ? arg3 + byte_sel : 0;

    // GR writes go to arg1, except the base write-back of ldi / sti (arg2)
    assign gr_write_sel = (gr_write_base == 1) ? arg2 + byte_sel : arg1 + byte_sel;

    assign bus_out = ((imm_read == 1) ? ((byte_sel == 0) ? imm_byte0 : imm_byte1) : 8'b0)
                   | ((off12_read == 1) ? ((byte_sel == 0) ? off12_byte0 : off12_byte1) : 8'b0);


    // Boot, in this order after configuration and after every global reset:
    //   1. the stabilization counter runs, the CPU idles
    //   2. until the boot counter is done every step is the boot ucode, which
    //      copies one byte, boot ROM -> bus -> memory; btrom_read also
    //      increments the boot counter
    //   3. normal operation
    localparam BOOT_UCODE = 16'h6928;   // btrom_read (MUX 1 = 13), mem_write (MUX 2 = 4), ucr (MUX 3 = 5)
    localparam UCR_UCODE  = 16'h0028;   // ucr (MUX 3 = 5) only: idle, back to fetch

    reg [22:0] counter = 0;

    always @(posedge clk) begin


        if (clk_phase == 0) begin
            // Fetch ucode from ROM
            if (counter[22] == 0) begin
                ucode <= UCR_UCODE;
                counter <= counter + 1;

                
                led_boot <= 1;
            end else if (btc_done_in == 0) begin
                ucode <= BOOT_UCODE;
            end else if ((irq_in == 1 && irqm_in == 1 && step == 0) == 1 && is_interrupted == 0) begin
                ucode <= UCR_UCODE;
                is_interrupted <= 1;

            end else if ((svc_in == 1 || pf_in == 1 || ini_in ==1) == 1 && is_interrupted == 0) begin
                ucode <= UCR_UCODE;
                is_interrupted <= 1;
            end else if ((svc_in == 1 || pf_in == 1 || ini_in ==1 || (irq_in == 1 && irqm_in == 1)) == 0 && is_interrupted == 1) begin
                ucode <= UCR_UCODE;
                is_interrupted <= 0;
            end else begin
                if (step < 11)
                    ucode <= fetch_ucode[step];
                else if (cond_met == 1)
                    ucode <= instr_ucode[(step - 11) | (opcode << 4)];
                else
                    ucode <= UCR_UCODE;


                led_boot <= 0;
        

            end

        end

        if (global_reset == 1) begin
            // ucode <= UCR_UCODE;
            // is_interrupted <= 0;
            counter <= 0;
        end

    end



    assign led_interrupted = is_interrupted;
    assign led_normal = ~is_interrupted;


    // ------------------------- END OF LOGIC ----------------------------- //



    UC uc (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),
        .reset(uc_reset),
        .step(step)
    );

    InstructionRegister ir (
        .clk(clk),
        .global_reset(global_reset),
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
        .imm_byte0_out(imm_byte0),
        .imm_byte1_out(imm_byte1),
        .off12_byte0_out(off12_byte0),
        .off12_byte1_out(off12_byte1)

    );



    initial begin
        $readmemh("control_rom.mem",instr_ucode);
        $readmemh("fetch.mem",fetch_ucode);
    end

endmodule
