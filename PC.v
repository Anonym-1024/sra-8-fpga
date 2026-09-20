
// Program counter

module PC (
    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire [7:0] bus_out,

    input wire byte_sel,
    input wire write_en,
    input wire read_en,
    input wire inc
);
    
    reg [7:0] byte_0 = 0;
    reg [7:0] byte_1 = 0;

    assign bus_out = (read_en == 1) ? (byte_sel == 0) ? byte_0 : byte_1 : 8'b0;

    always @(posedge clk) begin
        if (clk_phase == 2) begin
            if (inc ==1)
                {byte_1, byte_0} <=  {byte_1, byte_0} + 1;
            if (byte_sel == 0 && write_en == 1)
                byte_0 <= bus_in;
            if (byte_sel == 1 && write_en == 1)
                byte_1 <= bus_in;
        end

        if (global_reset == 1)
            {byte_1, byte_0} <= 0;
    end

endmodule