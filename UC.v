

module UC (
    input wire clk,
    input wire reset,
    output wire [3:0] step
);

    reg [3:0] content = 0;

    assign step = content;


    always @(posedge reset) begin
        content <= 0;
    end
    
    always @(negedge clk) begin
        content <= content + 1;
    end
    
endmodule
