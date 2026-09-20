



module INTR (
    input wire clk,
    input wire global_reset,
    input wire [1:0] clk_phase,

    output wire [7:0] bus_out,

    input wire irq_in,
    input wire ini_in,
    input wire svc_in,
    input wire pf_in,
    input wire int_in,

    output wire irq_out,
    output wire ini_out,
    output wire svc_out,
    output wire pf_out,

    input wire read_en,
    input wire reset
    
    
    
    
);
    
    reg [3:0] content = 0;

    assign bus_out = read_en ? {content, int_in} : 0;

    assign irq_out = content[3];
    assign svc_out = content[2];
    assign ini_out = content[1];
    assign pf_out = content[0];
    



    always @(posedge clk) begin
        if (irq_in == 1)
            content[3] <= 1;
        if (svc_in == 1)
            content[2] <= 1;
        if (pf_in == 1)
            content[1] <= 1;
        if (ini_in == 1)
            content[0] <= 1;
        if (clk_phase == 2) begin
            if (reset == 1)
                content <= 0;
        end

        if (global_reset == 1)
            content <= 0;
    end

endmodule