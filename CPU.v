

module CPU (
    input wire clk,
    input wire uart_rx,
    output wire uart_tx,
    input wire BTN2,
    output wire LED1,
    output wire LED4,
    output wire LED5,
    output wire LED2,
    output wire LED3

);

    assign LED2 = 0;
    assign LED4 = 0;

    initial begin
        $dumpfile("dump.vcd"); // Name of the waveform output file
        $dumpvars(0); // Dump all variables in module tb_counter and below


    end



    // PARAMETERS of the modules below

    localparam BOOT_FILE = "program.mem";       // BootROM: $readmemh image copied into memory at boot
    localparam BOOT_ADDR_BITS = 12;             // BootROM, BootCounter: 4096 bytes
    localparam INTPC_DEFAULT_VALUE = 0;         // INTPC: where interrupts start executing
    localparam UART_DIV = 1250;                 // Port: clk / baud = 12 MHz / 9600


    // GLOBAL RESET: BTN2.  It returns every register to its power-up value,
    // so the control unit runs the stabilization counter and the boot copy
    // again.  The button bounces; that only restarts the stabilization
    // counter a few more times.

    wire global_reset = BTN2;


    wire [1:0] clk_phase;

    Clock clock (
        .clk(clk),
        .phase(clk_phase)
    );


    // DATA BUS

    wire [7:0] bus;

    wire [7:0] registers_bus;
    wire [7:0] alu_bus;
    wire [7:0] memory_bus;
    wire [7:0] ptbr_bus;
    wire [7:0] psr_bus;
    wire [7:0] pc_bus;
    wire [7:0] intpc_bus;
    wire [7:0] intr_bus;
    wire [7:0] port_bus;
    wire [7:0] btrom_bus;
    wire [7:0] control_unit_bus;


    assign bus = registers_bus | alu_bus | memory_bus | ptbr_bus | psr_bus | pc_bus | intpc_bus | intr_bus | port_bus | btrom_bus | control_unit_bus;



    // ETC

    wire [3:0] flags;


    wire byte_sel;




    // GENERAL REGISTERS

    wire gr_read;
    wire gr_write;
    wire [3:0] gr_sel_read;
    wire [3:0] gr_sel_write;


    // ALU


    wire alu_read;
    wire alu_op1_write;
    wire alu_op2_write;
    wire [3:0] alu_opcode;
    wire [3:0] alu_flags_out;



    // MEMORY

    wire [15:0] mem_addr;


    wire mem_read;
    wire mem_write;



    // PTBR


    wire [15:0] ptbr_addr_out;

    wire ptbr_addr_read;
    wire [15:0] ptbr_output;

    wire ptbr_read;
    wire ptbr_write;




    // PTER


    wire [15:0] pter_addr_out;

    wire pter_addr_read;
    wire [15:0] pter_output;


    wire pter_write;



    // MAR

    wire [7:0] mar_addr_byte0_out;
    wire [7:0] mar_addr_byte1_out;
    wire [15:0] mar_addr_out;

    wire mar_write;
    wire mar_addr_read;
    wire [15:0] mar_output = mar_addr_read ? mar_addr_out : 0;



    // Boot ROM

    wire btrom_read;
    wire btc_done;
    wire [BOOT_ADDR_BITS-1:0] boot_addr;
    wire [15:0] boot_output = btrom_read ? boot_addr : 0;

    // Memory address assign
    assign mem_addr = mar_output | ptbr_output | pter_output | boot_output;

    assign ptbr_output = ptbr_addr_read ? ((ptbr_addr_out << 9) | (mar_addr_byte1_out << 1) | byte_sel) : 0;

    assign pter_output = pter_addr_read ? ((pter_addr_out << 8) | mar_addr_byte0_out) : 0;

    // PC

    wire pc_read;
    wire pc_write;
    wire pc_inc;



    // INTPC

    wire intpc_read;
    wire intpc_write;
    wire intpc_inc;


    // PSR

    wire [3:0] psr_flags_out;
    wire [1:0] pl;
    wire irqm_out;

    assign flags = psr_flags_out;

    wire psr_read;
    wire psr_write;
    wire psr_flags_write;



    // INTR

    wire intr_read;
    wire intr_write;
    wire irq_in;
    wire svc_in;
    reg ini_in = 0;
    reg pf_in = 0;
    wire int_in;

    wire irq_out;
    wire svc_out;
    wire pf_out;
    wire int_out;


    // Port

    wire port_read;
    wire port_write;

    // Control unit



    GeneralRegisters registers (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_out(registers_bus),
        .bus_in(bus),

        .write_en(gr_write),
        .read_en(gr_read),
        .read_sel(gr_sel_read),
        .write_sel(gr_sel_write)
    );


    ALU alu (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_out(alu_bus),
        .bus_in(bus),
        .flags_out(alu_flags_out),
        .flags_in(flags),

        .read_en(alu_read),
        .write_en(alu_op1_write),
        .write_en_2(alu_op2_write),
        .opcode(alu_opcode)

    );



    Memory memory (
        .clk(clk),
        .clk_phase(clk_phase),

        .bus_out(memory_bus),
        .bus_in(bus),
        .addr(mem_addr),

        .read_en(mem_read),
        .write_en(mem_write)

    );


    BootROM #(
        .ADDR_BITS(BOOT_ADDR_BITS),
        .FILE(BOOT_FILE)
    ) btrom (
        .clk(clk),

        .bus_out(btrom_bus),
        .addr(boot_addr),

        .read_en(btrom_read)
    );


    BootCounter #(
        .ADDR_BITS(BOOT_ADDR_BITS)
    ) btc (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .addr_out(boot_addr),
        .done_out(btc_done),

        .inc(btrom_read)                // every byte read moves on to the next one
    );


    PTBR ptbr (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_out(ptbr_bus),
        .bus_in(bus),
        .addr_out(ptbr_addr_out),

        .read_en(ptbr_read),
        .write_en(ptbr_write),
        .byte_sel(byte_sel)

    );




    PTER pter (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_in(bus),
        .addr_out(pter_addr_out),

        .write_en(pter_write),
        .byte_sel(byte_sel)

    );





    MAR mar (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_in(bus),
        .addr_out(mar_addr_out),
        .byte_0_out(mar_addr_byte0_out),
        .byte_1_out(mar_addr_byte1_out),

        .write_en(mar_write),
        .byte_sel(byte_sel)

    );


    PC pc (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_out(pc_bus),
        .bus_in(bus),

        .read_en(pc_read),
        .write_en(pc_write),
        .byte_sel(byte_sel),
        .inc(pc_inc)
    );



    INTPC #(
        .DEFAULT_VALUE(INTPC_DEFAULT_VALUE)
    ) intpc (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_out(intpc_bus),
        .bus_in(bus),

        .read_en(intpc_read),
        .write_en(intpc_write),
        .byte_sel(byte_sel),
        .inc(intpc_inc),
        .int_in(int_in)
    );



    PSR psr (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_out(psr_bus),
        .bus_in(bus),
        .flags_out(psr_flags_out),
        .flags_in(alu_flags_out),
        .pl_out(pl),
        .irqm_out(irqm_out),

        .read_en(psr_read),
        .write_en(psr_write),
        .flags_write_en(psr_flags_write)

    );



    INTR intr (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_out(intr_bus),
        .read_en(intr_read),

        .reset(intr_write),

        .irq_in(irq_in),
        .ini_in(ini_in),
        .svc_in(svc_in),
        .pf_in(pf_in),
        .int_in(int_in),

        .irq_out(irq_out),
        .ini_out(ini_out),
        .svc_out(svc_out),
        .pf_out(pf_out)
    );


    Port #(
        .UART_DIV(UART_DIV)
    ) port (
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),

        .bus_in(bus),
        .bus_out(port_bus),

        .uart_rx(uart_rx),
        .uart_tx(uart_tx),
        .irq_out(irq_in),

        .port_read(port_read),
        .port_write(port_write)
    );

    // CONTROL UNIT



    ControlUnit cu(
        .clk(clk),
        .global_reset(global_reset),
        .clk_phase(clk_phase),
        .bus_in(bus),
        .bus_out(control_unit_bus),
        .flags_in(flags),
        .pl_in(pl),



        // interrupts
        .irqm_in(irqm_out),
        .irq_in(irq_out),
        .svc_in(svc_out),
        .pf_in(pf_out),
        .ini_in(ini_out),
        .int_out(int_in),
        .svc_out(svc_in),

        // registers
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


        // port
        .port_read(port_read),
        .port_write(port_write),

        // status and interrupts
        .psr_read(psr_read),
        .psr_write(psr_write),
        .psr_flags_write(psr_flags_write),
        .intr_read(intr_read),
        .intr_write(intr_write),

        // boot
        .btc_done_in(btc_done),
        .btrom_read(btrom_read),


        .led_boot(LED5),
        .led_normal(LED1),
        .led_interrupted(LED3)

    );


endmodule
