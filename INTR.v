


module INTR (
    input wire read_en,
    input wire reset,
    input wire irq_in,
    input wire ini_in,
    input wire svc_in,
    input wire pf_in,
    output wire int_out,
    output wire [7:0] bus_out,
    input wire clk,
    input wire [1:0] clk_phase
);
    
    reg [4:0] content;

    assign bus_out = read_en ? content : 0;

    assign int_out = content[0];
/*
    always @(posedge irq_in) begin
        content[4] <= 1;
        
    end

    always @(posedge svc_in) begin
        content[3] <= 1;
        
    end

    always @(posedge ini_in) begin
        content[2] <= 1;
        
    end

    always @(posedge pf_in) begin
        content[1] <= 1;
        
    end
*/
    always @(posedge clk) begin
        if (reset == 1)
            content <= 0;

        if (content[4] == 1 || content[3] == 1 || content[2] == 1 || content[1] == 1) // mozna na negedge
            content[0] <= 1;
    end

endmodule