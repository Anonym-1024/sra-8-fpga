

module GeneralRegister #(
    parameter POSITION = 0
) (
    input wire read_en,
    input wire write_en,
    input wire [3:0] read_sel,
    input wire [3:0] write_sel,
    input wire clk,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out
);
    
    reg [7:0] content;


    assign bus_out = (read_sel == POSITION && read_en == 1) ? content : 0;


    always @(posedge clk) begin
        if (write_sel == POSITION && write_en == 1)
            content <= bus_in;
    end

endmodule