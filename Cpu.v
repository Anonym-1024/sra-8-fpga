

module CPU (
    inout wire [7:0] io,
    input wire clk
);


    wire [1:0] clk_phase;

    Clock clock (
        .clk(clk),
        .phase(clk_phase)
    );


    assign io = bus;
    

    wire [7:0] bus;

    wire [7:0] registers_bus;
    wire [7:0] alu_bus;
    wire [7:0] memory_bus;
    wire [7:0] ptbr_bus;
    wire [7:0] psr_bus;
    wire [7:0] pc_bus;
    wire [7:0] intpc_bus;
    wire [7:0] intr_bus;
    wire [7:0] control_unit_bus;


    assign bus = registers_bus | alu_bus | memory_bus | ptbr_bus | psr_bus | pc_bus | intpc_bus | intr_bus | control_unit_bus;
    


    wire [3:0] flags;

    assign flags = psr_flags_out;


    wire gr_read;
    wire gr_write;
    wire [3:0] gr_sel_read;
    wire [3:0] gr_sel_write;

    GeneralRegisters registers (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(registers_bus),
        .bus_in(bus),
        .write_en(gr_write),
        .read_en(gr_read),
        .read_sel(gr_sel_read),
        .write_sel(gr_sel_write)
    );




    wire alu_read;
    wire alu_op1_write;
    wire alu_op2_write;
    wire [3:0] alu_opcode;
    wire [3:0] alu_flags_out;

    ALU alu (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(alu_bus),
        .bus_in(bus),
        .read_en(alu_read),
        .write_en(alu_op1_write),
        .write_en_2(alu_op2_write),
        .opcode(alu_opcode),
        .flags_out(alu_flags_out),
        .flags_in(flags)
    );






    wire mem_read;
    wire mem_write;
    wire [15:0] mem_addr;

    assign mem_addr = mar_output | ptbr_output | pter_output;

    Memory memory (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(memory_bus),
        .bus_in(bus),
        .read_en(mem_read),
        .write_en(mem_write),
        .addr(mem_addr)
    );




    wire byte_sel;

    wire ptbr_read;
    wire ptbr_write;
    wire ptbr_addr_read;
    wire [15:0] ptbr_addr_out;

    wire [15:0] ptbr_output = ptbr_addr_read ? ((ptbr_addr_out << 9) | (mar_addr_byte1_out << 1) | byte_sel) : 0;

    PTBR ptbr (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(ptbr_bus),
        .bus_in(bus),
        .read_en(ptbr_read),
        .write_en(ptbr_write),
        .byte_sel(byte_sel),
        .addr_out(ptbr_addr_out)
    );




    
    wire pter_write;
    wire pter_addr_read;
    wire [15:0] pter_addr_out;

    wire [15:0] pter_output = pter_addr_read ? ((pter_addr_out << 8) | mar_addr_byte0_out) : 0;

    PTER pter (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_in(bus),
        .write_en(pter_write),
        .byte_sel(byte_sel),
        .addr_out(pter_addr_out)
    );



    wire mar_write;
    wire mar_addr_read;
    wire [7:0] mar_addr_byte0_out;
    wire [7:0] mar_addr_byte1_out;
    wire [15:0] mar_addr_out;

    wire [15:0] mar_output = mar_addr_read ? mar_addr_out : 0;

    MAR mar (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_in(bus),
        .write_en(mar_write),
        .byte_sel(byte_sel),
        .addr_out(mar_addr_out),
        .byte_0_out(mar_addr_byte0_out),
        .byte_1_out(mar_addr_byte1_out)
    );




    wire pc_read;
    wire pc_write;
    wire pc_inc;

    PC pc (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(pc_bus),
        .bus_in(bus),
        .read_en(pc_read),
        .write_en(pc_write),
        .byte_sel(byte_sel),
        .inc(pc_inc)
    );


    wire intpc_read;
    wire intpc_write;
    wire intpc_inc;


    INTPC intpc (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(intpc_bus),
        .bus_in(bus),
        .read_en(intpc_read),
        .write_en(intpc_write),
        .byte_sel(byte_sel),
        .inc(intpc_inc)
    );


    wire psr_read;
    wire psr_write;
    wire psr_flags_write;
    wire [3:0] psr_flags_out;

    PSR psr (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(psr_bus),
        .bus_in(bus),
        .read_en(psr_read),
        .write_en(psr_write),
        .flags_write_en(psr_flags_write),
        .flags_out(psr_flags_out),
        .flags_in(alu_flags_out)
    );



    wire intr_read;
    wire intr_write;
    wire irq;
    wire svc;
    wire ini;
    wire pf;
    wire int;
    

    INTR intr (
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_out(intr_bus),
        .read_en(intr_read),
        .reset(intr_write),
        .irq_in(irq),
        .ini_in(ini),
        .svc_in(svc),
        .pf_in(pf),
        .int_out(int)
    );

    




    ControlUnit cu(
        .clk(clk),
        .clk_phase(clk_phase),
        .bus_in(bus),
        .flags_in(flags),
        .int_in(int),
        .bus_out(control_unit_bus),

        .gr_read(gr_read),
        .gr_write(gr_write),
        .gr_read_sel(gr_sel_read),
        .gr_write_sel(gr_sel_write),

        // ALU
        .alu_read(alu_read),
        .alu_op1_write(alu_op1_write),
        .alu_op2_write(alu_op2_write),
        .alu_opcode(alu_opcode),

        // memory
        .mem_read(mem_read),
        .mem_write(mem_write),

        // MMU / addressing
        .ptbr_read(ptbr_read),
        .ptbr_write(ptbr_write),
        .ptbr_addr_read(ptbr_addr_read),
        .byte_sel(byte_sel),
        .pter_write(pter_write),
        .pter_addr_read(pter_addr_read),
        .mar_write(mar_write),
        .mar_addr_read(mar_addr_read),

        // program counters
        .pc_read(pc_read),
        .pc_write(pc_write),
        .pc_inc(pc_inc),
        .intpc_read(intpc_read),
        .intpc_write(intpc_write),
        .intpc_inc(intpc_inc),

        // status and interrupts
        .psr_read(psr_read),
        .psr_write(psr_write),
        .psr_flags_write(psr_flags_write),
        .intr_read(intr_read),
        .intr_write(intr_write),
        .svc(svc)
    );

    
endmodule