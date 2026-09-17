

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



    reg [7:0] outgoing = 0;
    assign port_out = outgoing;

    always @(posedge clk) begin
        if (clk_phase == 2)
            if (port_write == 1)
                outgoing <= bus_in;

        
    end

endmodule
