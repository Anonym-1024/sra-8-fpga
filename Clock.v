
// Clock

module Clock (
    input wire clk,
    output reg [1:0] phase = 0
);
    
    always @(posedge clk) begin
        phase <= phase + 1;
    end
endmodule