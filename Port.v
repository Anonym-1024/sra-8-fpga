

module Port (
    input wire clk,
    input wire [1:0] clk_phase,

    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    input wire [7:0] port_in,
    output wire [7:0] port_out,

    input wire port_write,
    input wire port_read
);



    reg [7:0] outgoing = 3;
    assign port_out = outgoing;
    reg a = 0;
    assign bus_out = a;

    always @(posedge clk) begin
        if (clk_phase == 2 && port_write) begin
            
                outgoing <= bus_in;
        end
        
    end

endmodule
