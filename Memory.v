

// Main RAM

module Memory (
    input wire clk,
    input wire [1:0] clk_phase,

    input wire [15:0] addr,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out,

    input wire read_en,
    input wire write_en
    
);


    reg [7:0] spram [0:(1<<12)-1];

    reg [7:0] read_buffer = 0;

    assign bus_out = (read_en == 1) ? read_buffer : 0;

    always @(posedge clk) begin
        if (clk_phase == 2 && write_en == 1) 
            spram[addr] <= bus_in;
        else  
            read_buffer <= spram[addr];
    end


    initial begin
        $readmemh("program.mem", spram);
    end

endmodule