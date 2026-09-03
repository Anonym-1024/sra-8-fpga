

module Memory (
    input wire read_en,
    input wire write_en,
    input wire clk,
    input wire [15:0] addr,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out
);


    reg [7:0] memory [0:(1<<16)];


    assign bus_out = (read_en == 1) ? memory[addr] : 0;

    always @(posedge clk) begin
        if (write_en == 1) 
            memory[addr] <= bus_in;
    end
    
endmodule