


module INTPC (
    input wire reset,
    input wire write_en_0,
    input wire write_en_1,
    input wire read_en_0,
    input wire read_en_1,
    input wire inc,
    input wire clk,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out
);
    
    reg [7:0] byte_0;
    reg [7:0] byte_1;

    assign bus_out = read_en_0 ? byte_0 :
                        read_en_1 ? byte_1 : 8'b0;


    always @(clk) begin
        if (reset == 1)
            {byte_1, byte_0} <= 0;
    end

    always @(posedge clk) begin
        if (inc ==1)
            {byte_1, byte_0} <= {byte_1, byte_0} + 1;
        if (write_en_0 == 1)
            byte_0 <= bus_in;
        if (write_en_1 == 1)
            byte_1 <= bus_in;
    end

endmodule