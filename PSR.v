

module PSR (
    input wire read_en,
    input wire write_en,
    input wire flags_write_en,
    input wire clk,
    input wire [1:0] clk_phase,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    input wire [3:0] flags_in,
    output wire [3:0] flags_out
);

    reg [7:0] content;

    assign bus_out = read_en ? content : 0;
    assign flags_out = content[3:0];


    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (write_en)
                content <= bus_in;
            if (flags_write_en)
                content[3:0] <= flags_in;
        end
    end

endmodule