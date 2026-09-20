

// U-Instruction counter

module UC (
    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    output wire [4:0] step,

    input wire reset
    
);

    reg [4:0] content = 0;

    assign step = content;


    
    
    always @(posedge clk) begin
        if (clk_phase == 3) begin // TODO: Try at phase 2
            content <= content + 1;
            if (reset == 1)
                content <= 0;
        end

        if (global_reset == 1)
            content <= 0;
    end
    
endmodule
