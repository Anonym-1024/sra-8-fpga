
/*
module FlagsRegister (
    input wire write_en,
    input wire clk,
    input wire [3:0] flags_in,
    output wire [3:0] flags_out
);
    
    reg [3:0] content;

    


    assign flags_out = content;


    always @(posedge clk) begin
        if (write_en == 1)
            content <= flags_in;
    end

endmodule

*/