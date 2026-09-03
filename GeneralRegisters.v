
module GeneralRegisters (
    input wire read_en,
    input wire write_en,
    input wire [3:0] read_sel,
    input wire [3:0] write_sel,
    input wire clk,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out
);

    wire [7:0] reg_bus_out [0:15];
 
    ALU alu ();

    genvar i;

    generate
        for (i = 0; i < 16; i = i + 1) begin

            

            GeneralRegister #(.POSITION(i)) registers (
                .read_en(read_en),
                .write_en(write_en),
                .read_sel(read_sel),
                .write_sel(write_sel),
                .clk(clk),
                .bus_in(bus_in),
                .bus_out(reg_bus_out[i])
            );
        end
    endgenerate


    

    assign bus_out = reg_bus_out[0] 
        | reg_bus_out[1] 
        | reg_bus_out[2] 
        | reg_bus_out[3] 
        | reg_bus_out[4] 
        | reg_bus_out[5] 
        | reg_bus_out[6] 
        | reg_bus_out[7] 
        | reg_bus_out[8] 
        | reg_bus_out[9] 
        | reg_bus_out[10] 
        | reg_bus_out[11] 
        | reg_bus_out[12] 
        | reg_bus_out[13] 
        | reg_bus_out[14] 
        | reg_bus_out[15];
    

endmodule