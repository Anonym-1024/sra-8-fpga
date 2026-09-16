

module UC (
    input wire clk,
    input wire [1:0] clk_phase,
    input wire reset,
    output wire [4:0] step
);

    reg [4:0] content = 0;

    assign step = content;


    
    
    always @(posedge clk) begin
        if (clk_phase == 3) begin
            content <= content + 1;
            if (reset == 1)
                content <= 0;
        end
    end
    
endmodule
