

module CPU (
    inout wire [7:0] io
);



    

    GeneralRegisters registers (

    );

    ALU alu (

    );

    Memory memory (

    );


    PTBR ptbr (

    );

    PTER pter (

    );

    PC pc (

    );


    PSR psr (

    );

    


    
endmodule