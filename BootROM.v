
// Boot ROM: the program image.  Main RAM is SPRAM, which cannot be
// initialized, so the control unit copies this ROM into it before the
// CPU starts (see BootCounter.v and the boot ucode in ControlUnit.v).

module BootROM #(
    parameter ADDR_BITS = 12,           // 4096 bytes, the size of program.mem
    parameter FILE = "program.mem"      // $readmemh image of the program
) (
    input wire clk,

    input wire [ADDR_BITS-1:0] addr,
    output wire [7:0] bus_out,

    input wire read_en
);

    reg [7:0] rom [0:(1<<ADDR_BITS)-1];

    reg [7:0] read_buffer = 0;

    assign bus_out = (read_en == 1) ? read_buffer : 0;

    always @(posedge clk) begin
        read_buffer <= rom[addr];
    end


    initial begin
        $readmemh(FILE, rom);
    end

endmodule
