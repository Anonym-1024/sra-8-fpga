

// U-Instruction counter

module UC (
    input wire clk,
    input wire [1:0] clk_phase,

    output wire [4:0] step,

    input wire reset
    
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
