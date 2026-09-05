

module CPU (
    inout wire [7:0] io,
    input wire clk
);

    

    wire [7:0] bus;

    wire [7:0] registers_bus;
    wire [7:0] alu_bus;
    wire [7:0] memory_bus;
    wire [7:0] ptbr_bus;
    wire [7:0] psr_bus;
    wire [7:0] pc_bus;
    wire [7:0] intpc_bus;
    wire [7:0] intr_bus;


    assign bus = registers_bus | alu_bus | memory_bus | ptbr_bus | psr_bus | pc_bus | intpc_bus | intr_bus;
    


    wire [3:0] flags;

    assign flags = psr_flags_out;


    wire gr_read;
    wire gr_write;
    wire [3:0] gr_read_sel;
    wire [3:0] gr_write_sel;

    GeneralRegisters registers (
        .clk(clk),
        .bus_out(registers_bus),
        .bus_in(bus),
        .write_en(gr_write),
        .read_en(gr_read),
        .read_sel(gr_read_sel),
        .write_sel(gr_write_sel)
    );




    wire alu_read_en;
    wire alu_write_op1;
    wire alu_write_op2;
    wire [3:0] alu_opcode;
    wire [3:0] alu_flags_out;

    ALU alu (
        .clk(clk),
        .bus_out(alu_bus),
        .bus_in(bus),
        .read_en(alu_read),
        .write_en_1(alu_write_op1),
        .write_en_2(alu_write_op2),
        .opcode(alu_opcode),
        .flags_out(alu_flags_out),
        .flags_in(flags)
    );






    wire mem_read;
    wire mem_write;
    wire [15:0] mem_addr;

    assign mem_addr = 0;

    Memory memory (
        .clk(clk),
        .bus_out(memory_bus),
        .bus_in(bus),
        .read_en(mem_read),
        .write_en(mem_write),
        .addr(mem_addr)
    );






    wire ptbr_read_byte0;
    wire ptbr_read_byte1;
    wire ptbr_write_byte0;
    wire ptbr_write_byte1;
    wire ptbr_addr_read;
    wire [15:0] ptbr_addr_out;


    PTBR ptbr (
        .clk(clk),
        .bus_out(ptbr_bus),
        .bus_in(bus),
        .read_en_0(ptbr_read_byte0),
        .read_en_1(ptbr_read_byte1),
        .write_en_0(ptbr_write_byte0),
        .write_en_1(ptbr_write_byte1),
        .addr_read_en(ptbr_addr_read),
        .addr_out(ptbr_addr_out)
    );




    
    wire pter_write_byte0;
    wire pter_write_byte1;
    wire pter_addr_read;
    wire [15:0] pter_addr_out;


    PTER pter (
        .clk(clk),
        .bus_in(bus),
        .write_en_0(pter_write_byte0),
        .write_en_1(pter_write_byte1),
        .addr_read_en(pter_addr_read),
        .addr_out(pter_addr_out)
    );



    wire mar_write_byte0;
    wire mar_write_byte1;
    wire mar_addr_read;
    wire [15:0] mar_addr_out;

    MAR mar (
        .clk(clk),
        .bus_in(bus),
        .write_en_0(mar_write_byte0),
        .write_en_1(mar_write_byte1),
        .addr_read_en(mar_addr_read),
        .addr_out(mar_addr_out)
    );




    wire pc_read_byte0;
    wire pc_read_byte1;
    wire pc_write_byte0;
    wire pc_write_byte1;
    wire pc_inc;

    PC pc (
        .clk(clk),
        .bus_out(pc_bus),
        .bus_in(bus),
        .read_en_0(pc_read_byte0),
        .read_en_1(pc_read_byte1),
        .write_en_0(pc_write_byte0),
        .write_en_1(pc_write_byte1),
        .inc(inc)
    );


    wire intpc_read_byte0;
    wire intpc_read_byte1;
    wire intpc_write_byte0;
    wire intpc_write_byte1;
    wire intpc_inc;


    INTPC intpc (
        .clk(clk),
        .bus_out(intpc_bus),
        .bus_in(bus),
        .read_en_0(intpc_read_byte0),
        .read_en_1(intpc_read_byte1),
        .write_en_0(intpc_write_byte0),
        .write_en_1(intpc_write_byte1),
        .inc(inc)
    );


    wire psr_read;
    wire psr_write;
    wire psr_flags_write;
    wire [3:0] psr_flags_out;

    PSR psr (
        .clk(clk),
        .bus_out(),
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
        .bus_in(bus),
        .flags_in(flags),
        .int_in(int)
    );

    
endmodule