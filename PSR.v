


// Process state register

module PSR (
    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    input wire [3:0] flags_in,
    output wire [3:0] flags_out,
    output wire [1:0] pl_out,
    output wire irqm_out,

    input wire read_en,
    input wire write_en,
    input wire flags_write_en
    
);

    reg [7:0] content = 8'b00000000;

    assign bus_out = read_en ? content : 0;

    assign flags_out = content[3:0];

    assign pl_out = content[7:6];
    assign irqm_out = content[5];

    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (write_en == 1)
                content <= bus_in;
            if (flags_write_en == 1)
                content[3:0] <= flags_in;
        end

        if (global_reset == 1)
            content <= 0;
    end

endmodule