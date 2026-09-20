
// Boot counter: the address of the byte that the control unit copies from
// the boot ROM into memory.  done_out goes high after the last byte.

module BootCounter #(
    parameter ADDR_BITS = 12            // size of the boot ROM, as in BootROM.v
) (
    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    output wire [ADDR_BITS-1:0] addr_out,
    output wire done_out,

    input wire inc
);

    reg [ADDR_BITS:0] content = 0;      // the top bit is set when the copy is done

    assign addr_out = content[ADDR_BITS-1:0];
    assign done_out = content[ADDR_BITS];

    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (inc == 1)
                content <= content + 1;
        end

        if (global_reset == 1)
            content <= 0;
    end

endmodule
