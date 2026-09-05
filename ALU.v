

module ALU (
    input wire read_en,
    input wire write_en_1,
    input wire write_en_2,
    input wire clk,
    input wire [3:0] opcode,
    input wire [7:0] bus_in,
    output wire [7:0] bus_out,
    input wire [3:0] flags_in,
    output wire [3:0] flags_out
);

    localparam OP_ADD = 0;
    localparam OP_ADDC = 1;
    localparam OP_SUB = 2;
    localparam OP_SUBC = 3;
    localparam OP_AND = 4;
    localparam OP_OR = 5;
    localparam OP_EOR = 6;
    localparam OP_LSL = 7;
    localparam OP_LSR = 8;
    localparam OP_ASR = 9;
    localparam OP_CSL = 10;
    localparam OP_CSR = 11;



    reg [7:0] reg_1;
    reg [7:0] reg_2;

    wire [7:0] result;

    wire [8:0] add_result;
    wire [8:0] addc_result;
    wire [8:0] sub_result;
    wire [8:0] subc_result;
    wire [7:0] and_result;
    wire [7:0] or_result;
    wire [7:0] eor_result;
    wire [7:0] lsl_result;
    wire [7:0] lsr_result;
    wire [7:0] asr_result;
    wire [7:0] csl_result;
    wire [7:0] csr_result;
    


    wire z;
    wire n;
    wire c;
    wire c_add;
    wire c_sub;
    wire c_addc;
    wire c_subc;
    wire c_lsl;
    wire c_lsr;
    wire c_asr;
    wire c_csr;
    wire c_csl;
    wire v;
    wire v_add;
    wire v_sub;
    wire v_addc;
    wire v_subc;

    assign add_result = reg_1 + reg_2;
    assign sub_result = reg_1 + (~reg_2 + 1);
    assign addc_result = reg_1 + reg_2 + flags_in[1];
    assign subc_result = reg_1 + (~reg_2 + 1) + flags_in[1];
    assign and_result = reg_1 & reg_2;
    assign or_result   = reg_1 | reg_2;
    assign eor_result  = reg_1 ^ reg_2;
    assign lsl_result  = reg_1 << 1;
    assign lsr_result  = reg_1 >> 1;
    assign asr_result  = {reg_1[7], reg_1[7:1]};
    assign csl_result  = {reg_1[6:0], flags_in[1]};
    assign csr_result  = {flags_in[1], reg_1[7:1]};

    assign result = (opcode == OP_ADD)  ? add_result  :
                        (opcode == OP_ADDC) ? addc_result :
                        (opcode == OP_SUB)  ? sub_result  :
                        (opcode == OP_SUBC) ? subc_result :
                        (opcode == OP_AND)  ? and_result  :
                        (opcode == OP_OR)   ? or_result   :
                        (opcode == OP_EOR)  ? eor_result  :
                        (opcode == OP_LSL)  ? lsl_result  :
                        (opcode == OP_LSR)  ? lsr_result  :
                        (opcode == OP_ASR)  ? asr_result  :
                        (opcode == OP_CSL)  ? csl_result  :
                        (opcode == OP_CSR)  ? csr_result  : 8'h00;




    
    assign z     = (result == 8'h00);
    assign n     = result[7];

   
    assign c_add = add_result[8];
    assign c_sub = ~sub_result[8];
    assign c_addc = addc_result[8];
    assign c_subc = ~subc_result[8];     
    assign c_lsl = reg_1[7];       
    assign c_lsr = reg_1[0];        
    assign c_asr = reg_1[0];       
    assign c_csl = reg_1[7];        
    assign c_csr = reg_1[0];       

    assign c     = (opcode == OP_ADD)  ? c_add :
                   (opcode == OP_ADDC) ? c_addc :
                   (opcode == OP_SUB)  ? c_sub :
                   (opcode == OP_SUBC) ? c_subc :
                   (opcode == OP_LSL)  ? c_lsl :
                   (opcode == OP_LSR)  ? c_lsr :
                   (opcode == OP_ASR)  ? c_asr :
                   (opcode == OP_CSL)  ? c_csl :
                   (opcode == OP_CSR)  ? c_csr : 0;

    assign v_add = (reg_1[7] == reg_2[7]) && (add_result[7] != reg_1[7]);
    assign v_sub = (reg_1[7] != reg_2[7]) && (sub_result[7] != reg_1[7]);
    assign v_addc = ((reg_1[7] == reg_2[7]) && (addc_result[7] != reg_1[7]));
    assign v_subc = ((reg_1[7] != reg_2[7]) && (subc_result[7] != reg_1[7]));

    assign v     = (opcode == OP_ADD)  ? v_add :
                   (opcode == OP_ADDC) ? v_addc : 
                   (opcode == OP_SUB)  ? v_sub : 
                   (opcode == OP_SUBC) ? v_subc : 1'b0;



    assign bus_out = (read_en == 1) ? result : 0;


    assign flags_out = {z, n, c, v};


    always @(posedge clk) begin
        if (write_en_1 == 1)
            reg_1 <= bus_in;
        if (write_en_2 == 1) 
            reg_2 <= bus_in;
    end


    
endmodule